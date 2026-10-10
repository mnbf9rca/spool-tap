#include <Arduino.h>
#include <unity.h>

#include "services.hpp"

namespace {
const char statusFixture[] =
#include "../fixtures/filabridge_status.inc"
;
const char spoolFixture[] = R"JSON({"id":7,"archived":false,"remaining_weight":165.75,"filament":{"vendor":{"name":"Buddy3d"},"name":"buddy3d PLA Black","material":"PLA","color_hex":"000000"}})JSON";

class FakeTransport : public HttpTransport {
public:
    FakeTransport() { response.status = 200; response.body = statusFixture; }
    HttpResponse response;
    bool succeed = true;
    ApiError failure = ApiError::Timeout;
    int calls = 0;
    std::string method;
    std::string url;
    std::string body;

    bool request(const char* sent_method, const char* sent_url, const std::string& sent_body,
                 HttpResponse& out, ApiError& error) override {
        ++calls;
        method = sent_method;
        url = sent_url;
        body = sent_body;
        if (!succeed) { error = failure; return false; }
        out = response;
        error = ApiError::None;
        return true;
    }
};

class MemoryStore : public Settings::Store {
public:
    ReadResult read(std::vector<uint8_t>&) override { return ReadResult::Missing; }
    bool write(const std::vector<uint8_t>&) override { return true; }
};

void configure(Settings& settings, const char* printer, int toolhead,
               const char* bridge = "https://filabridge.cynexia.net",
               const char* spoolman = "https://spoolman.cynexia.net") {
    MemoryStore store;
    SettingsInput input;
    input.printer_name = printer;
    input.toolhead_id = toolhead;
    input.filabridge_url = bridge;
    input.spoolman_url = spoolman;
    TEST_ASSERT_TRUE(settings.save(store, input));
}

std::string replaceOnce(std::string text, const char* from, const char* to) {
    const size_t at = text.find(from);
    TEST_ASSERT_TRUE(at != std::string::npos);
    text.replace(at, strlen(from), to);
    return text;
}

void test_mapping_uses_configured_printer_toolhead_and_url() {
    FakeTransport fake;
    Settings settings;
    configure(settings, "Side Printer", 2, "https://bridge.example:8443");
    Services services(fake, settings);
    Mapping mapping;
    ApiError error = ApiError::Http;
    TEST_ASSERT_TRUE(services.getMapping(mapping, error));
    TEST_ASSERT_EQUAL_UINT32(9, mapping.spool_id);
    TEST_ASSERT_FALSE(mapping.stale);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ApiError::None), static_cast<int>(error));
    TEST_ASSERT_EQUAL_STRING("GET", fake.method.c_str());
    TEST_ASSERT_EQUAL_STRING("https://bridge.example:8443/api/status", fake.url.c_str());
    TEST_ASSERT_TRUE(fake.body.empty());
    TEST_ASSERT_EQUAL_INT(1, fake.calls);
}

void test_zero_or_missing_toolhead_means_unloaded() {
    FakeTransport fake;
    Settings settings;
    Services services(fake, settings);
    Mapping mapping;
    mapping.spool_id = 44;
    mapping.stale = true;
    ApiError error;
    fake.response.body = replaceOnce(statusFixture, "\"spool_id\":7", "\"spool_id\":0");
    TEST_ASSERT_TRUE(services.getMapping(mapping, error));
    TEST_ASSERT_EQUAL_UINT32(0, mapping.spool_id);
    TEST_ASSERT_FALSE(mapping.stale);
    configure(settings, "Core One", 8);
    fake.response.body = statusFixture;
    TEST_ASSERT_TRUE(services.getMapping(mapping, error));
    TEST_ASSERT_EQUAL_UINT32(0, mapping.spool_id);
}

void test_duplicate_or_missing_printer_and_malformed_mapping_fail() {
    FakeTransport fake;
    Settings settings;
    Services services(fake, settings);
    Mapping mapping;
    mapping.spool_id = 44;
    mapping.stale = true;
    ApiError error;
    fake.response.body = replaceOnce(statusFixture, "Side Printer", "Core One");
    TEST_ASSERT_FALSE(services.getMapping(mapping, error));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ApiError::Malformed), static_cast<int>(error));
    TEST_ASSERT_EQUAL_UINT32(44, mapping.spool_id);
    configure(settings, "Absent", 0);
    fake.response.body = statusFixture;
    TEST_ASSERT_FALSE(services.getMapping(mapping, error));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ApiError::Malformed), static_cast<int>(error));
    configure(settings, "Core One", 0);
    fake.response.body = replaceOnce(statusFixture, "\"spool_id\":7", "\"spool_id\":-1");
    TEST_ASSERT_FALSE(services.getMapping(mapping, error));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ApiError::Malformed), static_cast<int>(error));
    fake.response.body = "{";
    TEST_ASSERT_FALSE(services.getMapping(mapping, error));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ApiError::Malformed), static_cast<int>(error));
    TEST_ASSERT_EQUAL_UINT32(44, mapping.spool_id);
    TEST_ASSERT_TRUE(mapping.stale);
}

void test_spool_fields_and_configured_url() {
    FakeTransport fake;
    fake.response.body = spoolFixture;
    Settings settings;
    configure(settings, "Core One", 0, "https://filabridge.cynexia.net", "https://spools.example:9443");
    Services services(fake, settings);
    SpoolInfo spool;
    ApiError error;
    TEST_ASSERT_TRUE(services.getSpool(7, spool, error));
    TEST_ASSERT_EQUAL_UINT32(7, spool.id);
    TEST_ASSERT_EQUAL_STRING("Buddy3d", spool.vendor.c_str());
    TEST_ASSERT_EQUAL_STRING("buddy3d PLA Black", spool.name.c_str());
    TEST_ASSERT_EQUAL_STRING("PLA", spool.material.c_str());
    TEST_ASSERT_EQUAL_STRING("000000", spool.color_hex.c_str());
    TEST_ASSERT_TRUE(spool.remaining_g > 165.749 && spool.remaining_g < 165.751);
    TEST_ASSERT_FALSE(spool.archived);
    TEST_ASSERT_EQUAL_STRING("GET", fake.method.c_str());
    TEST_ASSERT_EQUAL_STRING("https://spools.example:9443/api/v1/spool/7", fake.url.c_str());
    TEST_ASSERT_TRUE(fake.body.empty());
}

void test_missing_weight_is_unknown_but_bad_weight_is_rejected() {
    FakeTransport fake;
    Settings settings;
    Services services(fake, settings);
    SpoolInfo spool;
    ApiError error;
    fake.response.body = replaceOnce(spoolFixture, "165.75", "null");
    TEST_ASSERT_TRUE(services.getSpool(7, spool, error));
    TEST_ASSERT_TRUE(spool.remaining_g == -1);
    fake.response.body = replaceOnce(spoolFixture, "\"remaining_weight\":165.75,", "");
    TEST_ASSERT_TRUE(services.getSpool(7, spool, error));
    TEST_ASSERT_TRUE(spool.remaining_g == -1);
    fake.response.body = replaceOnce(spoolFixture, "165.75", "-2");
    TEST_ASSERT_FALSE(services.getSpool(7, spool, error));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ApiError::Malformed), static_cast<int>(error));
}

void test_null_vendor_and_color_are_optional() {
    FakeTransport fake;
    Settings settings;
    Services services(fake, settings);
    SpoolInfo spool;
    ApiError error;
    fake.response.body = replaceOnce(spoolFixture, "\"vendor\":{\"name\":\"Buddy3d\"}", "\"vendor\":null");
    fake.response.body = replaceOnce(fake.response.body, "\"color_hex\":\"000000\"", "\"color_hex\":null");
    TEST_ASSERT_TRUE(services.getSpool(7, spool, error));
    TEST_ASSERT_TRUE(spool.vendor.empty());
    TEST_ASSERT_TRUE(spool.color_hex.empty());
}

void test_missing_archived_and_malformed_spool_fail_without_overwriting() {
    FakeTransport fake;
    Settings settings;
    Services services(fake, settings);
    SpoolInfo spool;
    spool.id = 99;
    ApiError error;
    fake.response.status = 404;
    TEST_ASSERT_FALSE(services.getSpool(7, spool, error));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ApiError::MissingSpool), static_cast<int>(error));
    fake.response.status = 200;
    fake.response.body = replaceOnce(spoolFixture, "\"archived\":false", "\"archived\":true");
    TEST_ASSERT_FALSE(services.getSpool(7, spool, error));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ApiError::ArchivedSpool), static_cast<int>(error));
    fake.response.body = replaceOnce(spoolFixture, "\"material\":\"PLA\"", "\"material\":42");
    TEST_ASSERT_FALSE(services.getSpool(7, spool, error));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ApiError::Malformed), static_cast<int>(error));
    fake.response.body = "{";
    TEST_ASSERT_FALSE(services.getSpool(7, spool, error));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ApiError::Malformed), static_cast<int>(error));
    fake.response.body = "{\"id\":7,\"archived\":false}";
    TEST_ASSERT_FALSE(services.getSpool(7, spool, error));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ApiError::Malformed), static_cast<int>(error));
    fake.response.body = replaceOnce(spoolFixture, "\"id\":7", "\"id\":8");
    TEST_ASSERT_FALSE(services.getSpool(7, spool, error));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ApiError::Malformed), static_cast<int>(error));
    TEST_ASSERT_EQUAL_UINT32(99, spool.id);
}

void test_transport_errors_propagate_and_no_output_is_lost() {
    FakeTransport fake;
    fake.succeed = false;
    Settings settings;
    Services services(fake, settings);
    Mapping mapping;
    mapping.spool_id = 7;
    mapping.stale = true;
    ApiError error;
    TEST_ASSERT_FALSE(services.getMapping(mapping, error));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ApiError::Timeout), static_cast<int>(error));
    TEST_ASSERT_EQUAL_UINT32(7, mapping.spool_id);
    TEST_ASSERT_TRUE(mapping.stale);
}
} // namespace

void setup() {
    Serial.begin(115200);
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_mapping_uses_configured_printer_toolhead_and_url);
    RUN_TEST(test_zero_or_missing_toolhead_means_unloaded);
    RUN_TEST(test_duplicate_or_missing_printer_and_malformed_mapping_fail);
    RUN_TEST(test_spool_fields_and_configured_url);
    RUN_TEST(test_missing_weight_is_unknown_but_bad_weight_is_rejected);
    RUN_TEST(test_null_vendor_and_color_are_optional);
    RUN_TEST(test_missing_archived_and_malformed_spool_fail_without_overwriting);
    RUN_TEST(test_transport_errors_propagate_and_no_output_is_lost);
    UNITY_END();
}

void loop() {}
