#include <Arduino.h>
#include <unity.h>

#include "nfc_reader.hpp"

void test_uid_hex_keeps_nfc_a_and_nfc_v_display_order() {
    const uint8_t nfc_a[] = {0x04, 0x52, 0x7d, 0x12, 0x13, 0x6f, 0x81};
    const uint8_t nfc_v[] = {0xe0, 0x04, 0x01, 0x08, 0x66, 0x16, 0xdd, 0xd3};
    TEST_ASSERT_EQUAL_STRING("04527D12136F81", uidHex(nfc_a, sizeof(nfc_a)).c_str());
    TEST_ASSERT_EQUAL_STRING("E00401086616DDD3", uidHex(nfc_v, sizeof(nfc_v)).c_str());
    TEST_ASSERT_TRUE(uidHex(nullptr, 0).empty());
    const uint8_t too_long[11]{};
    TEST_ASSERT_TRUE(uidHex(too_long, sizeof(too_long)).empty());
}

void test_one_event_until_removal_survives_a_missed_read() {
    PresentationGate gate;
    TagRead tag;
    tag.uid = "E00401086616DDD3";
    tag.technology = TagTechnology::V;
    TEST_ASSERT_TRUE(gate.observe(&tag));
    TEST_ASSERT_FALSE(gate.observe(&tag));
    TEST_ASSERT_FALSE(gate.observe(nullptr));
    TEST_ASSERT_FALSE(gate.observe(&tag));
    TEST_ASSERT_FALSE(gate.observe(nullptr));
    TEST_ASSERT_FALSE(gate.observe(nullptr));
    TEST_ASSERT_TRUE(gate.observe(&tag));
}

void test_replacement_accepts_a_different_uid() {
    PresentationGate gate;
    TagRead first;
    first.uid = "1AA470F4";
    TagRead second;
    second.uid = "04527D12136F81";
    TEST_ASSERT_TRUE(gate.observe(&first));
    TEST_ASSERT_TRUE(gate.observe(&second));
    TEST_ASSERT_FALSE(gate.observe(&second));
    second.uid.clear();
    TEST_ASSERT_FALSE(gate.observe(&second));
}

void setup() {
    Serial.begin(115200);
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_uid_hex_keeps_nfc_a_and_nfc_v_display_order);
    RUN_TEST(test_one_event_until_removal_survives_a_missed_read);
    RUN_TEST(test_replacement_accepts_a_different_uid);
    UNITY_END();
}

void loop() {}
