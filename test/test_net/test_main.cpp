#include <Arduino.h>
#include <unity.h>

#include "net.hpp"

void test_clock_gate() {
    TEST_ASSERT_FALSE(clockValid(1735689599));
    TEST_ASSERT_TRUE(clockValid(1735689600));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ApiError::Wifi), static_cast<int>(readinessError(false, 1735689600)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ApiError::Time), static_cast<int>(readinessError(true, 0)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ApiError::None), static_cast<int>(readinessError(true, 1735689600)));
}

void test_distinct_error_labels() {
    TEST_ASSERT_EQUAL_STRING("Wi-Fi unavailable", apiErrorLabel(ApiError::Wifi));
    TEST_ASSERT_EQUAL_STRING("Time not set", apiErrorLabel(ApiError::Time));
    TEST_ASSERT_EQUAL_STRING("TLS certificate error", apiErrorLabel(ApiError::Tls));
    TEST_ASSERT_EQUAL_STRING("Request timed out", apiErrorLabel(ApiError::Timeout));
    TEST_ASSERT_NOT_EQUAL(0, strcmp(apiErrorLabel(ApiError::Wifi), apiErrorLabel(ApiError::Time)));
    TEST_ASSERT_NOT_EQUAL(0, strcmp(apiErrorLabel(ApiError::Tls), apiErrorLabel(ApiError::Timeout)));
}

void setup() {
    Serial.begin(115200);
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_clock_gate);
    RUN_TEST(test_distinct_error_labels);
    UNITY_END();
}

void loop() {}
