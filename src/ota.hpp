#pragma once

#include <cstddef>
#include <cstdint>

enum class UploadStage { Start, Write, End, Abort };
enum class OtaStatus { Idle, Receiving, Staged, Ready, NoSlot, Invalid, TooLarge, Incomplete, FlashFailed };

class OtaPort {
public:
    virtual ~OtaPort() = default;
    virtual bool inactiveSlot(size_t& capacity) = 0;
    virtual bool begin(size_t capacity) = 0;
    virtual size_t write(const uint8_t* bytes, size_t length) = 0;
    virtual bool finish(size_t written) = 0;
    virtual void abort() = 0;
    virtual bool pendingVerify(bool& pending) = 0;
    virtual bool markValid() = 0;
};

class Ota {
public:
    explicit Ota(OtaPort& port) : port_(port) {}
    OtaStatus handleUpload(UploadStage stage, const uint8_t* bytes = nullptr,
                           size_t length = 0, size_t reported_total = 0);
    OtaStatus commit();
    bool validateHealthyLoop();
    void dismissFailure();
    bool busy() const { return status_ == OtaStatus::Receiving; }
    bool ready() const { return status_ == OtaStatus::Ready; }
    bool staged() const { return status_ == OtaStatus::Staged; }
    bool failed() const { return status_ != OtaStatus::Idle && !busy() && !staged() && !ready(); }
    OtaStatus status() const { return status_; }
    size_t bytesWritten() const { return written_; }

private:
    OtaStatus fail(OtaStatus status);
    OtaPort& port_;
    OtaStatus status_ = OtaStatus::Idle;
    size_t capacity_ = 0;
    size_t written_ = 0;
    bool validated_ = false;
};

#ifndef SPOOL_TAP_UNIT_TEST
class ArduinoOtaPort : public OtaPort {
public:
    bool inactiveSlot(size_t& capacity) override;
    bool begin(size_t capacity) override;
    size_t write(const uint8_t* bytes, size_t length) override;
    bool finish(size_t written) override;
    void abort() override;
    bool pendingVerify(bool& pending) override;
    bool markValid() override;
};
#endif
