#include <Arduino.h>
#include <unity.h>

#include <cstring>

#include "settings.hpp"

namespace {
class MemoryStore : public Settings::Store {
public:
    ReadResult read(std::vector<uint8_t>&) override { return ReadResult::Missing; }
    bool write(const std::vector<uint8_t>&) override { return true; }
};

void test_page_renders_only_five_public_values_and_escapes_them() {
    Settings settings;
    MemoryStore store;
    SettingsInput input;
    input.printer_name = "A&B <Core> \"One\"";
    input.reader_id = "reader:1";
    TEST_ASSERT_TRUE(settings.save(store, input));
    const std::string html = renderSettingsPage(settings);
    TEST_ASSERT_NOT_NULL(strstr(html.c_str(), "https://spoolman.cynexia.net"));
    TEST_ASSERT_NOT_NULL(strstr(html.c_str(), "https://filabridge.cynexia.net"));
    TEST_ASSERT_NOT_NULL(strstr(html.c_str(), "A&amp;B &lt;Core&gt; &quot;One&quot;"));
    TEST_ASSERT_NOT_NULL(strstr(html.c_str(), "reader:1"));
    TEST_ASSERT_NOT_NULL(strstr(html.c_str(), "name=\"toolhead_id\""));
    TEST_ASSERT_NOT_NULL(strstr(html.c_str(), "Let's Encrypt"));
    TEST_ASSERT_NULL(strstr(html.c_str(), "WIFI_PASSWORD"));
    TEST_ASSERT_NULL(strstr(html.c_str(), "OTA_PASSWORD"));
    TEST_ASSERT_NULL(strstr(html.c_str(), "build_secrets"));
    TEST_ASSERT_NULL(strstr(html.c_str(), "Authorization"));
}

void test_page_shows_saved_record_fault_and_validation_error() {
    Settings settings;
    MemoryStore store;
    SettingsInput invalid;
    invalid.spoolman_url = "http://invalid";
    TEST_ASSERT_FALSE(settings.save(store, invalid));
    const std::string html = renderSettingsPage(settings, "Invalid <field>");
    TEST_ASSERT_NOT_NULL(strstr(html.c_str(), "Invalid Spoolman HTTPS origin"));
    TEST_ASSERT_NOT_NULL(strstr(html.c_str(), "Invalid &lt;field&gt;"));
    TEST_ASSERT_NULL(strstr(html.c_str(), "http://invalid"));
}

void test_origin_must_name_device_ip_and_match_host() {
    TEST_ASSERT_TRUE(sameDeviceOrigin("http://192.168.1.42", "192.168.1.42", "192.168.1.42"));
    TEST_ASSERT_TRUE(sameDeviceOrigin("http://192.168.1.42:80", "192.168.1.42:80", "192.168.1.42"));
    const char* bad_origins[] = {"", "http://other.example", "https://192.168.1.42",
                                 "http://192.168.1.42/", "null", "http://192.168.1.42:81"};
    for (const char* origin : bad_origins)
        TEST_ASSERT_FALSE(sameDeviceOrigin(origin, "192.168.1.42", "192.168.1.42"));
    TEST_ASSERT_FALSE(sameDeviceOrigin("http://evil.example", "evil.example", "192.168.1.42"));
    TEST_ASSERT_FALSE(sameDeviceOrigin("http://192.168.1.42", "evil.example", "192.168.1.42"));
}

void test_toolhead_form_number_is_strict_and_bounded() {
    int32_t value = -1;
    TEST_ASSERT_TRUE(parseToolheadId("0", value));
    TEST_ASSERT_EQUAL_INT(0, value);
    TEST_ASSERT_TRUE(parseToolheadId("2147483647", value));
    TEST_ASSERT_EQUAL_INT(INT32_MAX, value);
    const char* bad[] = {"", "-1", "+1", " 1", "1x", "1.0", "2147483648", "99999999999999999999"};
    for (const char* text : bad) TEST_ASSERT_FALSE(parseToolheadId(text, value));
}
}  // namespace

void setup() {
    Serial.begin(115200);
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_page_renders_only_five_public_values_and_escapes_them);
    RUN_TEST(test_page_shows_saved_record_fault_and_validation_error);
    RUN_TEST(test_origin_must_name_device_ip_and_match_host);
    RUN_TEST(test_toolhead_form_number_is_strict_and_bounded);
    UNITY_END();
}

void loop() {}
