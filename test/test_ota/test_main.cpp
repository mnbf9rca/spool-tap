#include <Arduino.h>
#include <unity.h>

#include "ota.hpp"

namespace {
class FakeOtaPort : public OtaPort {
public:
    bool slot = true;
    bool begin_ok = true;
    bool write_ok = true;
    bool finish_ok = true;
    bool pending = false;
    bool query_ok = true;
    bool mark_ok = true;
    size_t capacity = 128;
    size_t begin_calls = 0;
    size_t write_calls = 0;
    size_t finish_calls = 0;
    size_t abort_calls = 0;
    size_t mark_calls = 0;
    bool boot_target_changed = false;

    bool inactiveSlot(size_t& out) override { out = capacity; return slot; }
    bool begin(size_t) override { ++begin_calls; return begin_ok; }
    size_t write(const uint8_t*, size_t count) override {
        ++write_calls;
        return write_ok ? count : 0;
    }
    bool finish(size_t) override {
        ++finish_calls;
        if (finish_ok) boot_target_changed = true;
        return finish_ok;
    }
    void abort() override { ++abort_calls; }
    bool pendingVerify(bool& out) override { out = pending; return query_ok; }
    bool markValid() override { ++mark_calls; return mark_ok; }
};

uint8_t valid_header[32] = {0xE9};

void test_inactive_slot_is_required_before_writing() {
    FakeOtaPort port;
    port.slot = false;
    Ota ota(port);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(OtaStatus::NoSlot),
                          static_cast<int>(ota.handleUpload(UploadStage::Start)));
    TEST_ASSERT_EQUAL_UINT32(0, port.begin_calls);
    TEST_ASSERT_FALSE(port.boot_target_changed);
}

void test_oversize_and_invalid_images_abort_without_boot_change() {
    FakeOtaPort port;
    port.capacity = 32;
    Ota ota(port);
    ota.handleUpload(UploadStage::Start);
    uint8_t invalid[32] = {0};
    TEST_ASSERT_EQUAL_INT(static_cast<int>(OtaStatus::Invalid),
                          static_cast<int>(ota.handleUpload(UploadStage::Write, invalid, sizeof(invalid))));
    TEST_ASSERT_EQUAL_UINT32(0, port.write_calls);
    TEST_ASSERT_EQUAL_UINT32(1, port.abort_calls);
    ota.handleUpload(UploadStage::Start);
    ota.handleUpload(UploadStage::Write, valid_header, sizeof(valid_header));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(OtaStatus::TooLarge),
                          static_cast<int>(ota.handleUpload(UploadStage::Write, valid_header, 1)));
    TEST_ASSERT_EQUAL_UINT32(2, port.abort_calls);
    TEST_ASSERT_FALSE(port.boot_target_changed);
}

void test_partial_or_aborted_upload_never_activates() {
    FakeOtaPort port;
    Ota ota(port);
    ota.handleUpload(UploadStage::Start);
    ota.handleUpload(UploadStage::Write, valid_header, sizeof(valid_header));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(OtaStatus::Incomplete),
                          static_cast<int>(ota.handleUpload(UploadStage::End, nullptr, 0, 64)));
    TEST_ASSERT_EQUAL_UINT32(0, port.finish_calls);
    TEST_ASSERT_FALSE(port.boot_target_changed);
    ota.handleUpload(UploadStage::Start);
    ota.handleUpload(UploadStage::Write, valid_header, sizeof(valid_header));
    ota.handleUpload(UploadStage::Abort);
    TEST_ASSERT_EQUAL_UINT32(2, port.abort_calls);
    TEST_ASSERT_FALSE(port.boot_target_changed);
}

void test_flash_failure_keeps_current_boot_target() {
    FakeOtaPort port;
    port.write_ok = false;
    Ota ota(port);
    ota.handleUpload(UploadStage::Start);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(OtaStatus::FlashFailed),
                          static_cast<int>(ota.handleUpload(UploadStage::Write, valid_header, sizeof(valid_header))));
    TEST_ASSERT_FALSE(port.boot_target_changed);
    port.write_ok = true;
    port.finish_ok = false;
    ota.handleUpload(UploadStage::Start);
    ota.handleUpload(UploadStage::Write, valid_header, sizeof(valid_header));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(OtaStatus::Staged),
                          static_cast<int>(ota.handleUpload(UploadStage::End, nullptr, 0, 32)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(OtaStatus::Invalid), static_cast<int>(ota.commit()));
    TEST_ASSERT_FALSE(port.boot_target_changed);
}

void test_complete_image_activates_inactive_slot_only() {
    FakeOtaPort port;
    Ota ota(port);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(OtaStatus::Receiving),
                          static_cast<int>(ota.handleUpload(UploadStage::Start)));
    ota.handleUpload(UploadStage::Write, valid_header, sizeof(valid_header));
    TEST_ASSERT_TRUE(ota.busy());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(OtaStatus::Staged),
                          static_cast<int>(ota.handleUpload(UploadStage::End, nullptr, 0, 32)));
    TEST_ASSERT_FALSE(port.boot_target_changed);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(OtaStatus::Ready), static_cast<int>(ota.commit()));
    TEST_ASSERT_EQUAL_UINT32(1, port.finish_calls);
    TEST_ASSERT_TRUE(port.boot_target_changed);
    TEST_ASSERT_FALSE(ota.busy());
}

void test_second_file_cancels_staged_image_without_changing_boot_slot() {
    FakeOtaPort port;
    Ota ota(port);
    ota.handleUpload(UploadStage::Start);
    ota.handleUpload(UploadStage::Write, valid_header, sizeof(valid_header));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(OtaStatus::Staged),
                          static_cast<int>(ota.handleUpload(UploadStage::End, nullptr, 0, 32)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(OtaStatus::Invalid),
                          static_cast<int>(ota.handleUpload(UploadStage::Start)));
    TEST_ASSERT_EQUAL_UINT32(1, port.abort_calls);
    TEST_ASSERT_EQUAL_UINT32(0, port.finish_calls);
    TEST_ASSERT_FALSE(port.boot_target_changed);
}

void test_rollback_validation_waits_for_healthy_loop_and_retries_failure() {
    FakeOtaPort port;
    port.pending = true;
    port.mark_ok = false;
    Ota ota(port);
    TEST_ASSERT_EQUAL_UINT32(0, port.mark_calls);
    TEST_ASSERT_FALSE(ota.validateHealthyLoop());
    TEST_ASSERT_EQUAL_UINT32(1, port.mark_calls);
    port.mark_ok = true;
    TEST_ASSERT_TRUE(ota.validateHealthyLoop());
    TEST_ASSERT_TRUE(ota.validateHealthyLoop());
    TEST_ASSERT_EQUAL_UINT32(2, port.mark_calls);
}
}  // namespace

void setup() {
    Serial.begin(115200);
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_inactive_slot_is_required_before_writing);
    RUN_TEST(test_oversize_and_invalid_images_abort_without_boot_change);
    RUN_TEST(test_partial_or_aborted_upload_never_activates);
    RUN_TEST(test_flash_failure_keeps_current_boot_target);
    RUN_TEST(test_complete_image_activates_inactive_slot_only);
    RUN_TEST(test_second_file_cancels_staged_image_without_changing_boot_slot);
    RUN_TEST(test_rollback_validation_waits_for_healthy_loop_and_retries_failure);
    UNITY_END();
}

void loop() {}
