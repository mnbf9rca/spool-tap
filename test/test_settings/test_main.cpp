#include <Arduino.h>
#include <unity.h>

#include "settings.hpp"

namespace {
class MemoryStore : public Settings::Store {
public:
    ReadResult result = ReadResult::Missing;
    std::vector<uint8_t> blob;
    int writes = 0;
    bool fail_write = false;

    ReadResult read(std::vector<uint8_t>& bytes) override {
        if (result == ReadResult::Ok) bytes = blob;
        return result;
    }
    bool write(const std::vector<uint8_t>& bytes) override {
        ++writes;
        if (fail_write) return false;
        blob = bytes;
        result = ReadResult::Ok;
        return true;
    }
};

void test_defaults_and_missing_record() {
    MemoryStore store;
    Settings settings;
    TEST_ASSERT_TRUE(settings.load(store));
    TEST_ASSERT_FALSE(settings.fault());
    TEST_ASSERT_EQUAL_STRING("https://spoolman.cynexia.net", settings.current().spoolman_url.c_str());
    TEST_ASSERT_EQUAL_STRING("https://filabridge.cynexia.net", settings.current().filabridge_url.c_str());
    TEST_ASSERT_EQUAL_STRING("Core One", settings.current().printer_name.c_str());
    TEST_ASSERT_EQUAL_INT(0, settings.current().toolhead_id);
    TEST_ASSERT_EQUAL_STRING("spool-tap-cores3", settings.current().reader_id.c_str());
    TEST_ASSERT_EQUAL_STRING("Core One spool tap", settings.readerName().c_str());
}

void test_https_origins_and_atomic_validation() {
    MemoryStore store;
    Settings settings;
    SettingsInput input;
    input.spoolman_url = "https://spool.example:8443";
    TEST_ASSERT_TRUE(settings.save(store, input));
    TEST_ASSERT_EQUAL_INT(1, store.writes);
    TEST_ASSERT_EQUAL_STRING("https://spool.example:8443", settings.current().spoolman_url.c_str());
    const char* invalid[] = {"http://spool.example", "https://", "https://user@spool.example",
                             "https://spool.example/path", "https://spool.example/",
                             "https://spool.example?x=1", "https://spool.example#x",
                             "https://spool.example:0", "https://bad_host"};
    for (const char* url : invalid) {
        input.filabridge_url = url;
        TEST_ASSERT_FALSE(settings.save(store, input));
        TEST_ASSERT_EQUAL_INT(1, store.writes);
        TEST_ASSERT_EQUAL_STRING("https://spool.example:8443", settings.current().spoolman_url.c_str());
        TEST_ASSERT_FALSE(settings.error().empty());
    }
    input.filabridge_url = "https://bridge.example";
    input.spoolman_url = "https://spool.example:65536";
    TEST_ASSERT_FALSE(settings.save(store, input));
    TEST_ASSERT_EQUAL_INT(1, store.writes);
}

void test_name_reader_and_toolhead_bounds() {
    MemoryStore store;
    Settings settings;
    SettingsInput input;
    input.printer_name.assign(54, 'A');
    input.reader_id.assign(64, 'x');
    input.toolhead_id = INT32_MAX;
    TEST_ASSERT_TRUE(settings.save(store, input));
    TEST_ASSERT_EQUAL_UINT32(64, settings.readerName().size());
    TEST_ASSERT_EQUAL_INT(1, store.writes);
    input.printer_name.assign(55, 'A');
    TEST_ASSERT_FALSE(settings.save(store, input));
    input.printer_name = "";
    TEST_ASSERT_FALSE(settings.save(store, input));
    input.printer_name = std::string("\xC0\xAF", 2);
    TEST_ASSERT_FALSE(settings.save(store, input));
    input.printer_name = "Core One";
    input.reader_id = "bad/id";
    TEST_ASSERT_FALSE(settings.save(store, input));
    input.reader_id.assign(65, 'x');
    TEST_ASSERT_FALSE(settings.save(store, input));
    input.reader_id = "ok._:-9";
    input.toolhead_id = -1;
    TEST_ASSERT_FALSE(settings.save(store, input));
    TEST_ASSERT_EQUAL_INT(1, store.writes);
    TEST_ASSERT_EQUAL_INT(INT32_MAX, settings.current().toolhead_id);
    input.printer_name.clear();
    for (int i = 0; i < 27; ++i) input.printer_name += "é";
    input.toolhead_id = 0;
    TEST_ASSERT_TRUE(settings.save(store, input));
    TEST_ASSERT_EQUAL_UINT32(54, settings.current().printer_name.size());
    TEST_ASSERT_EQUAL_STRING("ok._:-9", settings.current().reader_id.c_str());
}

void test_save_reload_and_failed_write() {
    MemoryStore store;
    Settings settings;
    SettingsInput input;
    input.printer_name = "Workshop";
    input.toolhead_id = 2;
    input.reader_id = "tap:2";
    TEST_ASSERT_TRUE(settings.save(store, input));
    TEST_ASSERT_TRUE(store.blob.size() > 8);
    const std::string serialized(store.blob.begin(), store.blob.end());
    TEST_ASSERT_TRUE(serialized.find("Workshop") != std::string::npos);
    TEST_ASSERT_TRUE(serialized.find("WIFI_") == std::string::npos);
    TEST_ASSERT_TRUE(serialized.find("OTA_PASSWORD") == std::string::npos);
    Settings reloaded;
    TEST_ASSERT_TRUE(reloaded.load(store));
    TEST_ASSERT_FALSE(reloaded.fault());
    TEST_ASSERT_EQUAL_STRING("Workshop", reloaded.current().printer_name.c_str());
    TEST_ASSERT_EQUAL_INT(2, reloaded.current().toolhead_id);
    TEST_ASSERT_EQUAL_STRING("tap:2", reloaded.current().reader_id.c_str());
    store.fail_write = true;
    input.printer_name = "Should not apply";
    const auto before = store.blob;
    TEST_ASSERT_FALSE(reloaded.save(store, input));
    TEST_ASSERT_EQUAL_STRING("Workshop", reloaded.current().printer_name.c_str());
    TEST_ASSERT_EQUAL_MEMORY(before.data(), store.blob.data(), before.size());
}

void test_corrupt_newer_and_unreadable_fallback() {
    MemoryStore store;
    Settings settings;
    store.result = Settings::Store::ReadResult::Ok;
    store.blob = {'b', 'a', 'd'};
    TEST_ASSERT_FALSE(settings.load(store));
    TEST_ASSERT_TRUE(settings.fault());
    TEST_ASSERT_FALSE(settings.error().empty());
    TEST_ASSERT_EQUAL_STRING("Core One", settings.current().printer_name.c_str());
    store.blob = {'{', '"', 'v', '"', ':', '2', '}'};
    TEST_ASSERT_FALSE(settings.load(store));
    TEST_ASSERT_TRUE(settings.fault());
    const char invalid_record[] = "{\"v\":1,\"spoolman\":\"http://bad\",\"filabridge\":\"https://bridge.example\",\"printer\":\"Core One\",\"toolhead\":0,\"reader\":\"tap\"}";
    store.blob.assign(invalid_record, invalid_record + strlen(invalid_record));
    TEST_ASSERT_FALSE(settings.load(store));
    TEST_ASSERT_TRUE(settings.fault());
    store.result = Settings::Store::ReadResult::Error;
    TEST_ASSERT_FALSE(settings.load(store));
    TEST_ASSERT_TRUE(settings.fault());
    store.result = Settings::Store::ReadResult::Missing;
    TEST_ASSERT_TRUE(settings.load(store));
    TEST_ASSERT_FALSE(settings.fault());
}
}  // namespace

void setup() {
    Serial.begin(115200);
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_defaults_and_missing_record);
    RUN_TEST(test_https_origins_and_atomic_validation);
    RUN_TEST(test_name_reader_and_toolhead_bounds);
    RUN_TEST(test_save_reload_and_failed_write);
    RUN_TEST(test_corrupt_newer_and_unreadable_fallback);
    UNITY_END();
}

void loop() {}
