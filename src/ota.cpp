#include "ota.hpp"

#ifndef SPOOL_TAP_UNIT_TEST
#include <Update.h>
#include <esp_ota_ops.h>
#endif

namespace {
constexpr size_t kMinimumImageBytes = 24;
constexpr uint8_t kEspImageMagic = 0xE9;
}

OtaStatus Ota::fail(OtaStatus status) {
    if (busy() || staged()) port_.abort();
    status_ = status;
    return status_;
}

OtaStatus Ota::handleUpload(UploadStage stage, const uint8_t* bytes,
                            size_t length, size_t reported_total) {
    if (stage == UploadStage::Start) {
        if (ready() || staged()) return fail(OtaStatus::Invalid);
        if (busy()) return fail(OtaStatus::Incomplete);
        written_ = 0;
        capacity_ = 0;
        if (!port_.inactiveSlot(capacity_) || capacity_ < kMinimumImageBytes)
            return status_ = OtaStatus::NoSlot;
        if (!port_.begin(capacity_)) {
            port_.abort();
            return status_ = OtaStatus::FlashFailed;
        }
        return status_ = OtaStatus::Receiving;
    }
    if (stage == UploadStage::Abort) return fail(OtaStatus::Incomplete);
    if (!busy()) return status_;
    if (stage == UploadStage::Write) {
        if (!length) return status_;
        if (!bytes || (written_ == 0 && bytes[0] != kEspImageMagic))
            return fail(OtaStatus::Invalid);
        if (length > capacity_ - written_) return fail(OtaStatus::TooLarge);
        if (port_.write(bytes, length) != length) return fail(OtaStatus::FlashFailed);
        written_ += length;
        return status_;
    }
    if (stage == UploadStage::End) {
        if (reported_total != written_ || written_ < kMinimumImageBytes)
            return fail(OtaStatus::Incomplete);
        return status_ = OtaStatus::Staged;
    }
    return status_;
}

OtaStatus Ota::commit() {
    if (!staged()) return status_;
    if (!port_.finish(written_)) return fail(OtaStatus::Invalid);
    return status_ = OtaStatus::Ready;
}

bool Ota::validateHealthyLoop() {
    if (validated_) return true;
    bool pending = false;
    if (!port_.pendingVerify(pending)) return false;
    if (pending && !port_.markValid()) return false;
    validated_ = true;
    return true;
}

void Ota::dismissFailure() {
    if (failed()) status_ = OtaStatus::Idle;
}

#ifndef SPOOL_TAP_UNIT_TEST
extern "C" bool verifyRollbackLater() { return true; }

bool ArduinoOtaPort::inactiveSlot(size_t& capacity) {
    const esp_partition_t* running = esp_ota_get_running_partition();
    const esp_partition_t* target = esp_ota_get_next_update_partition(nullptr);
    if (!running || !target || target->address == running->address) return false;
    capacity = target->size;
    return true;
}

bool ArduinoOtaPort::begin(size_t capacity) {
    return Update.begin(capacity, U_FLASH);
}

size_t ArduinoOtaPort::write(const uint8_t* bytes, size_t length) {
    return Update.write(const_cast<uint8_t*>(bytes), length);
}

bool ArduinoOtaPort::finish(size_t) {
    // Update.end verifies the complete ESP image before choosing the next boot slot.
    return Update.end(true);
}

void ArduinoOtaPort::abort() { Update.abort(); }

bool ArduinoOtaPort::pendingVerify(bool& pending) {
    const esp_partition_t* running = esp_ota_get_running_partition();
    if (!running) return false;
    esp_ota_img_states_t state;
    const esp_err_t error = esp_ota_get_state_partition(running, &state);
    if (error == ESP_ERR_NOT_SUPPORTED || error == ESP_ERR_NOT_FOUND) {
        pending = false;
        return true;
    }
    if (error != ESP_OK) return false;
    pending = state == ESP_OTA_IMG_PENDING_VERIFY;
    return true;
}

bool ArduinoOtaPort::markValid() {
    return esp_ota_mark_app_valid_cancel_rollback() == ESP_OK;
}
#endif
