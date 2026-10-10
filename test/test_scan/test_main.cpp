#include <Arduino.h>
#include <cJSON.h>
#include <unity.h>

#include <cstring>

#include "services.hpp"

namespace {
const char unknownReply[] = R"JSON({"uid":"A1B2","reader_id":"reader-test","matched_spool_id":null,"matched_filament_id":null})JSON";
const char spoolReply[] = R"JSON({"uid":"A1B2","reader_id":"reader-test","matched_spool_id":7,"matched_filament_id":null,"spool":{"id":7,"archived":false,"remaining_weight":165.75,"filament":{"vendor":{"name":"Buddy3d"},"name":"buddy3d PLA Black","material":"PLA","color_hex":"000000"}}})JSON";

class FakeTransport : public HttpTransport {
public:
    FakeTransport() { response.status = 200; response.body = unknownReply; }
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

void configure(Settings& settings) {
    MemoryStore store;
    SettingsInput input;
    input.spoolman_url = "https://spools.example:9443";
    input.printer_name = "Core One";
    input.reader_id = "reader-test";
    TEST_ASSERT_TRUE(settings.save(store, input));
}

TagRead tag() {
    TagRead result;
    result.uid = "a1b2";
    result.type = "NFC-V";
    result.format = "OpenPrintTag";
    return result;
}

std::string replaceOnce(std::string text, const char* from, const char* to) {
    const size_t at = text.find(from);
    TEST_ASSERT_TRUE(at != std::string::npos);
    text.replace(at, strlen(from), to);
    return text;
}

void test_configured_scan_request_has_uid_reader_name_format_and_no_payload() {
    FakeTransport fake;
    Settings settings;
    configure(settings);
    Services services(fake, settings);
    ScanMatch match;
    ApiError error;
    TEST_ASSERT_TRUE(services.scan(tag(), match, error));
    TEST_ASSERT_EQUAL_INT(1, fake.calls);
    TEST_ASSERT_EQUAL_STRING("POST", fake.method.c_str());
    TEST_ASSERT_EQUAL_STRING("https://spools.example:9443/api/v1/tag/scan", fake.url.c_str());
    cJSON* body = cJSON_Parse(fake.body.c_str());
    TEST_ASSERT_NOT_NULL(body);
    TEST_ASSERT_EQUAL_STRING("A1B2", cJSON_GetObjectItemCaseSensitive(body, "uid")->valuestring);
    TEST_ASSERT_EQUAL_STRING("reader-test", cJSON_GetObjectItemCaseSensitive(body, "reader_id")->valuestring);
    TEST_ASSERT_EQUAL_STRING("Core One spool tap", cJSON_GetObjectItemCaseSensitive(body, "name")->valuestring);
    TEST_ASSERT_EQUAL_STRING("OpenPrintTag", cJSON_GetObjectItemCaseSensitive(body, "format")->valuestring);
    TEST_ASSERT_NULL(cJSON_GetObjectItemCaseSensitive(body, "payload_b64"));
    cJSON_Delete(body);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ScanKind::Unknown), static_cast<int>(match.kind));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ApiError::None), static_cast<int>(error));
}

void test_linked_spool_uses_embedded_spool_preview() {
    FakeTransport fake;
    fake.response.body = spoolReply;
    Settings settings;
    configure(settings);
    Services services(fake, settings);
    ScanMatch match;
    ApiError error;
    TEST_ASSERT_TRUE(services.scan(tag(), match, error));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ScanKind::Spool), static_cast<int>(match.kind));
    TEST_ASSERT_EQUAL_UINT32(7, match.spool_id);
    TEST_ASSERT_EQUAL_UINT32(7, match.spool.id);
    TEST_ASSERT_EQUAL_STRING("Buddy3d", match.spool.vendor.c_str());
    TEST_ASSERT_EQUAL_STRING("buddy3d PLA Black", match.spool.name.c_str());
    TEST_ASSERT_EQUAL_STRING("PLA", match.spool.material.c_str());
    TEST_ASSERT_TRUE(match.spool.remaining_g > 165.749 && match.spool.remaining_g < 165.751);
    TEST_ASSERT_EQUAL_INT(1, fake.calls);
}

void test_filament_only_and_unknown_have_no_spool() {
    FakeTransport fake;
    Settings settings;
    configure(settings);
    Services services(fake, settings);
    ScanMatch match;
    ApiError error;
    fake.response.body = replaceOnce(unknownReply, "\"matched_filament_id\":null", "\"matched_filament_id\":42");
    TEST_ASSERT_TRUE(services.scan(tag(), match, error));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ScanKind::FilamentOnly), static_cast<int>(match.kind));
    TEST_ASSERT_EQUAL_UINT32(42, match.filament_id);
    TEST_ASSERT_EQUAL_UINT32(0, match.spool_id);
    fake.response.body = unknownReply;
    TEST_ASSERT_TRUE(services.scan(tag(), match, error));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ScanKind::Unknown), static_cast<int>(match.kind));
    TEST_ASSERT_EQUAL_UINT32(0, match.filament_id);
    TEST_ASSERT_EQUAL_UINT32(0, match.spool_id);
}

void test_missing_or_contradictory_match_fields_are_malformed() {
    FakeTransport fake;
    Settings settings;
    configure(settings);
    Services services(fake, settings);
    ScanMatch match;
    match.spool_id = 99;
    ApiError error;
    fake.response.body = replaceOnce(unknownReply, "\"matched_spool_id\":null,", "");
    TEST_ASSERT_FALSE(services.scan(tag(), match, error));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ApiError::Malformed), static_cast<int>(error));
    fake.response.body = replaceOnce(spoolReply, "\"matched_filament_id\":null", "\"matched_filament_id\":42");
    TEST_ASSERT_FALSE(services.scan(tag(), match, error));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ApiError::Malformed), static_cast<int>(error));
    fake.response.body = replaceOnce(spoolReply, "\"matched_spool_id\":7", "\"matched_spool_id\":8");
    TEST_ASSERT_FALSE(services.scan(tag(), match, error));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ApiError::Malformed), static_cast<int>(error));
    fake.response.body = replaceOnce(unknownReply, "\"matched_filament_id\":null", "\"matched_filament_id\":0");
    TEST_ASSERT_FALSE(services.scan(tag(), match, error));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ApiError::Malformed), static_cast<int>(error));
    fake.response.body = replaceOnce(unknownReply, "\"matched_spool_id\":null", "\"matched_spool_id\":1.5");
    TEST_ASSERT_FALSE(services.scan(tag(), match, error));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ApiError::Malformed), static_cast<int>(error));
    fake.response.body = replaceOnce(unknownReply, "\"matched_spool_id\":null", "\"matched_spool_id\":null,\"spool\":{}");
    TEST_ASSERT_FALSE(services.scan(tag(), match, error));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ApiError::Malformed), static_cast<int>(error));
    TEST_ASSERT_EQUAL_UINT32(99, match.spool_id);
}

void test_timeout_keeps_previous_match_and_does_not_retry() {
    FakeTransport fake;
    fake.succeed = false;
    Settings settings;
    configure(settings);
    Services services(fake, settings);
    ScanMatch match;
    match.spool_id = 99;
    ApiError error;
    TEST_ASSERT_FALSE(services.scan(tag(), match, error));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ApiError::Timeout), static_cast<int>(error));
    TEST_ASSERT_EQUAL_UINT32(99, match.spool_id);
    TEST_ASSERT_EQUAL_INT(1, fake.calls);
}

void test_bad_uid_and_archived_spool_cannot_be_loaded() {
    FakeTransport fake;
    Settings settings;
    configure(settings);
    Services services(fake, settings);
    ScanMatch match;
    match.spool_id = 99;
    ApiError error;
    TagRead unreadable = tag();
    unreadable.uid = "A1: B2";
    TEST_ASSERT_FALSE(services.scan(unreadable, match, error));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ApiError::InvalidTag), static_cast<int>(error));
    TEST_ASSERT_EQUAL_INT(0, fake.calls);
    fake.response.body = replaceOnce(spoolReply, "\"archived\":false", "\"archived\":true");
    TEST_ASSERT_FALSE(services.scan(tag(), match, error));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ApiError::ArchivedSpool), static_cast<int>(error));
    TEST_ASSERT_EQUAL_UINT32(99, match.spool_id);
}

void test_bad_echo_and_broken_json_are_malformed() {
    FakeTransport fake;
    Settings settings;
    configure(settings);
    Services services(fake, settings);
    ScanMatch match;
    ApiError error;
    fake.response.body = replaceOnce(unknownReply, "\"uid\":\"A1B2\"", "\"uid\":\"A1B3\"");
    TEST_ASSERT_FALSE(services.scan(tag(), match, error));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ApiError::Malformed), static_cast<int>(error));
    fake.response.body = "{";
    TEST_ASSERT_FALSE(services.scan(tag(), match, error));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ApiError::Malformed), static_cast<int>(error));
}
} // namespace

void setup() {
    Serial.begin(115200);
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_configured_scan_request_has_uid_reader_name_format_and_no_payload);
    RUN_TEST(test_linked_spool_uses_embedded_spool_preview);
    RUN_TEST(test_filament_only_and_unknown_have_no_spool);
    RUN_TEST(test_missing_or_contradictory_match_fields_are_malformed);
    RUN_TEST(test_timeout_keeps_previous_match_and_does_not_retry);
    RUN_TEST(test_bad_uid_and_archived_spool_cannot_be_loaded);
    RUN_TEST(test_bad_echo_and_broken_json_are_malformed);
    UNITY_END();
}

void loop() {}
