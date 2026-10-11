#include <Arduino.h>
#include <cJSON.h>
#include <unity.h>

#include <cstring>

#include "services.hpp"

namespace {
const char statusFixture[] =
#include "../fixtures/filabridge_status.inc"
;
const char mapFixture[] =
#include "../fixtures/filabridge_map.inc"
;

struct Step {
    Step(bool succeeds, int status, const std::string& body,
         ApiError failure = ApiError::Timeout)
        : succeeds(succeeds), status(status), body(body), failure(failure) {}
    bool succeeds;
    int status;
    std::string body;
    ApiError failure;
};

class FakeTransport : public HttpTransport {
public:
    struct Call { std::string method; std::string url; std::string body; };
    std::vector<Step> steps;
    std::vector<Call> calls;

    bool request(const char* method, const char* url, const std::string& body,
                 HttpResponse& out, ApiError& error) override {
        calls.push_back({method, url, body});
        if (calls.size() > steps.size()) { error = ApiError::Http; return false; }
        const Step& step = steps[calls.size() - 1];
        if (!step.succeeds) { error = step.failure; return false; }
        out.status = step.status;
        out.body = step.body;
        error = ApiError::None;
        return true;
    }
};

FakeTransport* pump_transport = nullptr;
size_t pump_after_calls = 0;
void recordPump() { pump_after_calls = pump_transport->calls.size(); }

class MemoryStore : public Settings::Store {
public:
    ReadResult read(std::vector<uint8_t>&) override { return ReadResult::Missing; }
    bool write(const std::vector<uint8_t>&) override { return true; }
};

void configure(Settings& settings) {
    MemoryStore store;
    SettingsInput input;
    input.filabridge_url = "https://bridge.example:8443";
    input.printer_name = "Side Printer";
    input.toolhead_id = 2;
    TEST_ASSERT_TRUE(settings.save(store, input));
}

std::string statusWithSpool(uint32_t id) {
    std::string body = statusFixture;
    const size_t at = body.find("\"spool_id\":9");
    TEST_ASSERT_TRUE(at != std::string::npos);
    body.replace(at, strlen("\"spool_id\":9"), "\"spool_id\":" + std::to_string(id));
    return body;
}

void assertCalls(const FakeTransport& fake, uint32_t spool_id) {
    TEST_ASSERT_EQUAL_UINT32(2, fake.calls.size());
    TEST_ASSERT_EQUAL_STRING("POST", fake.calls[0].method.c_str());
    TEST_ASSERT_EQUAL_STRING("https://bridge.example:8443/api/map_toolhead", fake.calls[0].url.c_str());
    cJSON* request = cJSON_Parse(fake.calls[0].body.c_str());
    TEST_ASSERT_NOT_NULL(request);
    TEST_ASSERT_TRUE(cJSON_IsObject(request));
    int fields = 0;
    for (const cJSON* field = request->child; field; field = field->next) ++fields;
    TEST_ASSERT_EQUAL_INT(3, fields);
    const cJSON* printer = cJSON_GetObjectItemCaseSensitive(request, "printer_name");
    const cJSON* toolhead = cJSON_GetObjectItemCaseSensitive(request, "toolhead_id");
    const cJSON* spool = cJSON_GetObjectItemCaseSensitive(request, "spool_id");
    TEST_ASSERT_TRUE(cJSON_IsString(printer));
    TEST_ASSERT_TRUE(cJSON_IsNumber(toolhead));
    TEST_ASSERT_TRUE(cJSON_IsNumber(spool));
    TEST_ASSERT_EQUAL_STRING("Side Printer", printer->valuestring);
    TEST_ASSERT_EQUAL_INT(2, toolhead->valueint);
    TEST_ASSERT_EQUAL_UINT32(spool_id, spool->valueint);
    cJSON_Delete(request);
    TEST_ASSERT_EQUAL_STRING("GET", fake.calls[1].method.c_str());
    TEST_ASSERT_EQUAL_STRING("https://bridge.example:8443/api/status", fake.calls[1].url.c_str());
    TEST_ASSERT_TRUE(fake.calls[1].body.empty());
}

void test_load_posts_configured_mapping_and_reads_back_target() {
    FakeTransport fake;
    fake.steps = {{true, 200, mapFixture}, {true, 200, statusWithSpool(9)}};
    Settings settings;
    configure(settings);
    Services services(fake, settings);
    Mapping after;
    after.spool_id = 77;
    ApiError error = ApiError::Http;
    TEST_ASSERT_TRUE(services.mapToolhead(9, after, error));
    assertCalls(fake, 9);
    TEST_ASSERT_EQUAL_UINT32(9, after.spool_id);
    TEST_ASSERT_FALSE(after.stale);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ApiError::None), static_cast<int>(error));
}

void test_unload_posts_zero_and_reads_back_unloaded() {
    FakeTransport fake;
    fake.steps = {{true, 200, R"JSON({"message":"Toolhead unmapped successfully"})JSON"},
                  {true, 200, statusWithSpool(0)}};
    Settings settings;
    configure(settings);
    Services services(fake, settings);
    Mapping after;
    after.spool_id = 9;
    ApiError error;
    TEST_ASSERT_TRUE(services.mapToolhead(0, after, error));
    assertCalls(fake, 0);
    TEST_ASSERT_EQUAL_UINT32(0, after.spool_id);
    TEST_ASSERT_FALSE(after.stale);
}

void test_rejected_posts_still_refresh_mapping_without_claiming_success() {
    for (int status : {400, 404, 409}) {
        FakeTransport fake;
        fake.steps = {{true, status, R"JSON({"error":"rejected"})JSON"},
                      {true, 200, statusWithSpool(7)}};
        Settings settings;
        configure(settings);
        Services services(fake, settings);
        Mapping after;
        after.spool_id = 77;
        ApiError error;
        TEST_ASSERT_FALSE(services.mapToolhead(9, after, error));
        assertCalls(fake, 9);
        TEST_ASSERT_EQUAL_UINT32(7, after.spool_id);
        TEST_ASSERT_EQUAL_INT(static_cast<int>(status == 409 ? ApiError::Conflict : ApiError::Http),
                              static_cast<int>(error));
    }
}

void test_timed_out_post_that_applied_is_confirmed_by_get_without_retry() {
    FakeTransport fake;
    fake.steps = {{false, 0, "", ApiError::Timeout}, {true, 200, statusWithSpool(9)}};
    Settings settings;
    configure(settings);
    Services services(fake, settings);
    Mapping after;
    ApiError error;
    TEST_ASSERT_TRUE(services.mapToolhead(9, after, error));
    assertCalls(fake, 9);
    TEST_ASSERT_EQUAL_UINT32(9, after.spool_id);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ApiError::None), static_cast<int>(error));
}

void test_timed_out_post_with_unchanged_or_failed_get_stays_uncertain() {
    FakeTransport fake;
    fake.steps = {{false, 0, "", ApiError::Timeout}, {true, 200, statusWithSpool(7)}};
    Settings settings;
    configure(settings);
    Services services(fake, settings);
    Mapping after;
    after.spool_id = 77;
    ApiError error;
    TEST_ASSERT_FALSE(services.mapToolhead(9, after, error));
    assertCalls(fake, 9);
    TEST_ASSERT_EQUAL_UINT32(7, after.spool_id);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ApiError::Timeout), static_cast<int>(error));
    fake.calls.clear();
    fake.steps = {{false, 0, "", ApiError::Timeout}, {false, 0, "", ApiError::Timeout}};
    after.spool_id = 77;
    TEST_ASSERT_FALSE(services.mapToolhead(9, after, error));
    assertCalls(fake, 9);
    TEST_ASSERT_EQUAL_UINT32(77, after.spool_id);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ApiError::Timeout), static_cast<int>(error));
}

void test_malformed_post_or_status_does_not_claim_success() {
    FakeTransport fake;
    fake.steps = {{true, 200, "{"}, {true, 200, statusWithSpool(9)}};
    Settings settings;
    configure(settings);
    Services services(fake, settings);
    Mapping after;
    after.spool_id = 77;
    ApiError error;
    TEST_ASSERT_FALSE(services.mapToolhead(9, after, error));
    assertCalls(fake, 9);
    TEST_ASSERT_EQUAL_UINT32(9, after.spool_id);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ApiError::Malformed), static_cast<int>(error));
    fake.calls.clear();
    fake.steps = {{true, 200, mapFixture}, {true, 200, "{"}};
    after.spool_id = 77;
    TEST_ASSERT_FALSE(services.mapToolhead(9, after, error));
    assertCalls(fake, 9);
    TEST_ASSERT_EQUAL_UINT32(77, after.spool_id);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ApiError::Malformed), static_cast<int>(error));
}

void test_successful_post_with_wrong_readback_is_not_confirmed() {
    FakeTransport fake;
    fake.steps = {{true, 200, mapFixture}, {true, 200, statusWithSpool(7)}};
    Settings settings;
    configure(settings);
    Services services(fake, settings);
    Mapping after;
    ApiError error;
    TEST_ASSERT_FALSE(services.mapToolhead(9, after, error));
    assertCalls(fake, 9);
    TEST_ASSERT_EQUAL_UINT32(7, after.spool_id);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ApiError::Malformed), static_cast<int>(error));
}

void test_web_pump_runs_between_post_and_readback() {
    FakeTransport fake;
    fake.steps = {{true, 200, mapFixture}, {true, 200, statusWithSpool(9)}};
    Settings settings;
    configure(settings);
    Services services(fake, settings);
    pump_transport = &fake;
    pump_after_calls = 0;
    services.setBetweenRequests(recordPump);
    Mapping after;
    ApiError error;
    TEST_ASSERT_TRUE(services.mapToolhead(9, after, error));
    TEST_ASSERT_EQUAL_UINT32(1, pump_after_calls);
    pump_transport = nullptr;
}
} // namespace

void setup() {
    Serial.begin(115200);
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_load_posts_configured_mapping_and_reads_back_target);
    RUN_TEST(test_unload_posts_zero_and_reads_back_unloaded);
    RUN_TEST(test_rejected_posts_still_refresh_mapping_without_claiming_success);
    RUN_TEST(test_timed_out_post_that_applied_is_confirmed_by_get_without_retry);
    RUN_TEST(test_timed_out_post_with_unchanged_or_failed_get_stays_uncertain);
    RUN_TEST(test_malformed_post_or_status_does_not_claim_success);
    RUN_TEST(test_successful_post_with_wrong_readback_is_not_confirmed);
    RUN_TEST(test_web_pump_runs_between_post_and_readback);
    UNITY_END();
}

void loop() {}
