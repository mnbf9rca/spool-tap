#include <Arduino.h>
#include <unity.h>

void test_usb_cdc_unity_output() { TEST_ASSERT_EQUAL_INT(42, 6 * 7); }

void setup() {
    Serial.begin(115200);
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_usb_cdc_unity_output);
    UNITY_END();
}

void loop() {}
