#include <Arduino.h>
#include <unity.h>

#include "tag_decode.hpp"

namespace {
const uint8_t sample[] = {
#include "../fixtures/openprinttag_sample.inc"
};

NdefRecord record(const char* type, std::initializer_list<uint8_t> bytes) {
    NdefRecord result;
    result.tnf = 2;
    result.type = type;
    result.payload.assign(bytes);
    return result;
}

NdefRecord json(const char* body) {
    NdefRecord result;
    result.tnf = 2;
    result.type = "application/json";
    result.payload.assign(body, body + strlen(body));
    return result;
}

void test_sample_hints_are_descriptive_only() {
    TagRead tag;
    tag.uid = "E00401086616DDD3";
    tag.technology = TagTechnology::V;
    tag.capacity = 320;
    tag.block_size = 4;
    tag.memory.assign(sample, sample + sizeof(sample));
    std::vector<NdefRecord> records;
    TEST_ASSERT_TRUE(extractNdef(tag, records));
    TagHints hints;
    TEST_ASSERT_TRUE(decodeTag(records, hints));
    TEST_ASSERT_EQUAL_STRING("OpenPrintTag", hints.format.c_str());
    TEST_ASSERT_EQUAL_STRING("Prusament", hints.brand.c_str());
    TEST_ASSERT_EQUAL_STRING("PLA Galaxy Black", hints.material.c_str());
    TEST_ASSERT_EQUAL_STRING("PLA", hints.material_type.c_str());
    TEST_ASSERT_EQUAL_STRING("3D3E3D", hints.color_hex.c_str());
    TEST_ASSERT_EQUAL_STRING("473BB8CDE12945B89FCFDA1C3ADD9C47", hints.identifier.c_str());
    TEST_ASSERT_EQUAL_INT(1000, static_cast<int>(hints.nominal_weight_g));
    TEST_ASSERT_EQUAL_STRING("E00401086616DDD3", tag.uid.c_str());
}

void test_unsorted_unknown_nested_keys_and_absent_optional_fields() {
    // Meta map is empty. Main map: unknown nested field before and after known fields.
    const auto opt = record("application/vnd.openprinttag", {
        0xA0, 0xA4, 0x18, 0x63, 0xA1, 0x01, 0x82, 0x01, 0x02,
        0x0B, 0x61, 'X', 0x0A, 0x63, 'P', 'L', 'A', 0x18, 0x64, 0xF5
    });
    TagHints hints;
    TEST_ASSERT_TRUE(decodeTag({opt}, hints));
    TEST_ASSERT_EQUAL_STRING("X", hints.brand.c_str());
    TEST_ASSERT_EQUAL_STRING("PLA", hints.material.c_str());
    TEST_ASSERT_TRUE(hints.color_hex.empty());
    TEST_ASSERT_TRUE(hints.identifier.empty());
    TEST_ASSERT_EQUAL_INT(0, static_cast<int>(hints.nominal_weight_g));
}

void test_empty_aux_and_truncated_regions() {
    // Meta {aux offset: 9}, main {brand: "X"}, empty auxiliary map.
    auto tag = record("application/vnd.openprinttag", {
        0xA1, 0x02, 0x09, 0xA1, 0x0B, 0x61, 'X', 0x00, 0x00, 0xA0
    });
    TagHints hints;
    TEST_ASSERT_TRUE(decodeTag({tag}, hints));
    TEST_ASSERT_EQUAL_STRING("X", hints.brand.c_str());
    tag.payload[2] = 0x40;
    TEST_ASSERT_FALSE(decodeTag({tag}, hints));
    TEST_ASSERT_TRUE(hints.brand.empty());
    tag.payload = {0xA0, 0xA1, 0x0B, 0x63, 'X'};
    TEST_ASSERT_FALSE(decodeTag({tag}, hints));
}

void test_openspool_and_bad_json() {
    TagHints hints;
    TEST_ASSERT_TRUE(decodeTag({json("{\"protocol\":\"openspool\",\"version\":\"1.0\",\"brand\":\"Generic\",\"type\":\"PLA\",\"color_hex\":\"FFAABB\",\"min_temp\":\"220\",\"max_temp\":\"240\"}")}, hints));
    TEST_ASSERT_EQUAL_STRING("OpenSpool", hints.format.c_str());
    TEST_ASSERT_EQUAL_STRING("Generic", hints.brand.c_str());
    TEST_ASSERT_EQUAL_STRING("PLA", hints.material_type.c_str());
    TEST_ASSERT_EQUAL_STRING("FFAABB", hints.color_hex.c_str());
    TEST_ASSERT_EQUAL_STRING("220", hints.min_temp_c.c_str());
    TEST_ASSERT_EQUAL_STRING("240", hints.max_temp_c.c_str());
    TEST_ASSERT_FALSE(decodeTag({json("{\"protocol\":\"openspool\",\"version\":\"2.0\"}")}, hints));
    TEST_ASSERT_FALSE(decodeTag({json("{\"protocol\":\"openspool\",\"version\":\"1.0\"")}, hints));
    TEST_ASSERT_FALSE(decodeTag({json("{\"protocol\":\"openspool\",\"version\":\"1.0\",\"color_hex\":\"GGGGGG\"}")}, hints));
    TEST_ASSERT_TRUE(hints.brand.empty());
}

void test_selects_supported_mime_after_unrelated_record() {
    const auto unrelated = json("{\"protocol\":\"other\"}");
    const auto supported = record("application/vnd.openprinttag", {0xA0, 0xA1, 0x0B, 0x61, 'X'});
    TagHints hints;
    TEST_ASSERT_TRUE(decodeTag({unrelated, supported}, hints));
    TEST_ASSERT_EQUAL_STRING("X", hints.brand.c_str());
    auto wrong_tnf = supported;
    wrong_tnf.tnf = 1;
    TEST_ASSERT_FALSE(decodeTag({wrong_tnf}, hints));
}

void test_half_float_nominal_and_json_depth_limit() {
    const auto half = record("application/vnd.openprinttag", {
        0xA0, 0xA1, 0x10, 0xF9, 0x63, 0xD0
    });
    TagHints hints;
    TEST_ASSERT_TRUE(decodeTag({half}, hints));
    TEST_ASSERT_EQUAL_INT(1000, static_cast<int>(hints.nominal_weight_g));
    std::string nested = "{\"protocol\":\"openspool\",\"version\":\"1.0\",\"extra\":";
    nested.append(20, '[');
    nested += '0';
    nested.append(20, ']');
    nested += '}';
    TEST_ASSERT_FALSE(decodeTag({json(nested.c_str())}, hints));
}
}  // namespace

void setup() {
    Serial.begin(115200);
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_sample_hints_are_descriptive_only);
    RUN_TEST(test_unsorted_unknown_nested_keys_and_absent_optional_fields);
    RUN_TEST(test_empty_aux_and_truncated_regions);
    RUN_TEST(test_openspool_and_bad_json);
    RUN_TEST(test_selects_supported_mime_after_unrelated_record);
    RUN_TEST(test_half_float_nominal_and_json_depth_limit);
    UNITY_END();
}

void loop() {}
