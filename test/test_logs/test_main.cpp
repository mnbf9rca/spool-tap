#include <cstring>
#include <type_traits>

#include "logs.hpp"
#include <unity.h>

void setUp() {}
void tearDown() {}

void test_ring_keeps_newest_one_hundred_timestamped_events() {
    Logs logs;
    for (int i = 0; i < 101; ++i)
        logs.add(LogLevel::Info, i % 2 ? LogEvent::TagScanned : LogEvent::MappingReady,
                 1760140800 + i);
    TEST_ASSERT_EQUAL_UINT32(100, logs.size());
    const std::string html = logs.renderHtml();
    TEST_ASSERT_NULL(strstr(html.c_str(), "00:00:00 UTC"));
    TEST_ASSERT_NOT_NULL(strstr(html.c_str(), "00:00:01 UTC"));
    TEST_ASSERT_NOT_NULL(strstr(html.c_str(), "00:01:40 UTC"));
    TEST_ASSERT_TRUE(html.find("00:00:01 UTC") < html.find("00:01:40 UTC"));
}

void test_html_escapes_all_dynamic_text_characters() {
    TEST_ASSERT_EQUAL_STRING("&lt;tag a=&quot;x&quot;&gt;Tom &amp; &#39;Bob&#39;&lt;/tag&gt;",
                             escapeHtml("<tag a=\"x\">Tom & 'Bob'</tag>").c_str());
}

void test_page_has_safe_events_and_no_secret_input_channel() {
    static_assert(!std::is_convertible<const char*, LogEvent>::value,
                  "raw payloads and credentials must not be log events");
    Logs logs;
    logs.add(LogLevel::Warning, LogEvent::ScanFailed, 1760140800);
    const std::string html = logs.renderHtml();
    TEST_ASSERT_NOT_NULL(strstr(html.c_str(), "Tag scan failed"));
    TEST_ASSERT_NOT_NULL(strstr(html.c_str(), "2025-10-11"));
    TEST_ASSERT_NULL(strstr(html.c_str(), "Authorization"));
    TEST_ASSERT_NULL(strstr(html.c_str(), "password"));
    TEST_ASSERT_NULL(strstr(html.c_str(), "NDEF"));
}

void test_context_is_escaped_and_bounded_in_ram() {
    Logs logs;
    logs.add(LogLevel::Info, LogEvent::TagScanned,
             "UID=A1B2 brand=<Prusa>&\"' " + std::string(300, 'x'), 1760140800);
    const std::string html = logs.renderHtml();
    TEST_ASSERT_NOT_NULL(strstr(html.c_str(), "UID=A1B2"));
    TEST_ASSERT_NOT_NULL(strstr(html.c_str(), "brand=&lt;Prusa&gt;&amp;&quot;&#39;"));
    TEST_ASSERT_NULL(strstr(html.c_str(), "<Prusa>"));
    TEST_ASSERT_NULL(strstr(html.c_str(), std::string(300, 'x').c_str()));
}

void setup() {
    UNITY_BEGIN();
    RUN_TEST(test_ring_keeps_newest_one_hundred_timestamped_events);
    RUN_TEST(test_html_escapes_all_dynamic_text_characters);
    RUN_TEST(test_page_has_safe_events_and_no_secret_input_channel);
    RUN_TEST(test_context_is_escaped_and_bounded_in_ram);
    UNITY_END();
}

void loop() {}
