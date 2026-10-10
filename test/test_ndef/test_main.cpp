#include <Arduino.h>
#include <unity.h>

#include "tag_ndef.hpp"

namespace {
const uint8_t sample[] = {
#include "../fixtures/openprinttag_sample.inc"
};

TagRead sampleTag() {
    TagRead tag;
    tag.uid = "E00401086616DDD3";
    tag.technology = TagTechnology::V;
    tag.capacity = 320;
    tag.block_size = 4;
    tag.memory.assign(sample, sample + sizeof(sample));
    return tag;
}

TagRead type2Tag() {
    TagRead tag;
    tag.uid = "04527D12136F81";
    tag.technology = TagTechnology::A;
    tag.capacity = 504;
    tag.block_size = 4;
    tag.memory = {0xE1, 0x10, 0x3F, 0x00, 0x00, 0xFD, 0x02, 0xAA, 0xBB,
                  0x03, 0x0E, 0xD2, 0x0A, 0x01};
    const char type[] = "text/plain";
    tag.memory.insert(tag.memory.end(), type, type + 10);
    tag.memory.push_back('x');
    tag.memory.push_back(0xFE);
    while (tag.memory.size() % 4) tag.memory.push_back(0);
    return tag;
}
}  // namespace

void test_type5_sample_extended_tlv_and_mime() {
    TagRead tag = sampleTag();
    std::vector<NdefRecord> records;
    TEST_ASSERT_EQUAL_UINT32(304, sizeof(sample));
    TEST_ASSERT_TRUE(extractNdef(tag, records));
    TEST_ASSERT_EQUAL_UINT32(1, records.size());
    TEST_ASSERT_EQUAL_STRING("application/vnd.openprinttag", records[0].type.c_str());
    TEST_ASSERT_EQUAL_UINT8(2, records[0].tnf);
    TEST_ASSERT_EQUAL_UINT32(261, records[0].payload.size());
}

void test_type2_short_tlv_skips_other_tlvs() {
    TagRead tag = type2Tag();
    std::vector<NdefRecord> records;
    TEST_ASSERT_TRUE(extractNdef(tag, records));
    TEST_ASSERT_EQUAL_UINT32(1, records.size());
    TEST_ASSERT_EQUAL_STRING("text/plain", records[0].type.c_str());
    TEST_ASSERT_EQUAL_UINT8('x', records[0].payload[0]);
    tag.capacity = 503;
    TEST_ASSERT_FALSE(extractNdef(tag, records));
    TEST_ASSERT_TRUE(records.empty());
}

void test_multiple_records_keep_mime_selection_available() {
    TagRead tag;
    tag.uid = "04527D12136F81";
    tag.technology = TagTechnology::A;
    tag.capacity = 504;
    tag.block_size = 4;
    tag.memory = {0xE1, 0x10, 0x3F, 0x00, 0x03, 0x1B,
                  0x91, 0x01, 0x02, 'U', 0x03, 'x',
                  0x52, 0x10, 0x02};
    const char type[] = "application/json";
    tag.memory.insert(tag.memory.end(), type, type + 16);
    tag.memory.push_back('{');
    tag.memory.push_back('}');
    tag.memory.push_back(0xFE);
    while (tag.memory.size() % 4) tag.memory.push_back(0);
    std::vector<NdefRecord> records;
    TEST_ASSERT_TRUE(extractNdef(tag, records));
    TEST_ASSERT_EQUAL_UINT32(2, records.size());
    TEST_ASSERT_EQUAL_STRING("U", records[0].type.c_str());
    TEST_ASSERT_EQUAL_STRING("application/json", records[1].type.c_str());
    TEST_ASSERT_EQUAL_UINT32(2, records[1].payload.size());
}

void test_truncated_or_over_capacity_type5_keeps_uid() {
    TagRead tag = sampleTag();
    std::vector<NdefRecord> records;
    tag.memory.resize(40);
    TEST_ASSERT_FALSE(extractNdef(tag, records));
    TEST_ASSERT_TRUE(records.empty());
    TEST_ASSERT_EQUAL_STRING("E00401086616DDD3", tag.uid.c_str());
    tag = sampleTag();
    tag.capacity = 300;
    TEST_ASSERT_FALSE(extractNdef(tag, records));
    tag = sampleTag();
    tag.block_size = 5;
    TEST_ASSERT_FALSE(extractNdef(tag, records));
    tag = sampleTag();
    tag.memory[6] = 0x02;
    TEST_ASSERT_FALSE(extractNdef(tag, records));
    tag = sampleTag();
    tag.memory[10] = 0x7F;
    TEST_ASSERT_FALSE(extractNdef(tag, records));
}

void setup() {
    Serial.begin(115200);
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_type5_sample_extended_tlv_and_mime);
    RUN_TEST(test_type2_short_tlv_skips_other_tlvs);
    RUN_TEST(test_multiple_records_keep_mime_selection_available);
    RUN_TEST(test_truncated_or_over_capacity_type5_keeps_uid);
    UNITY_END();
}

void loop() {}
