#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include <M5Unified.h>
#include <M5UnitUnifiedNFC.h>
#include <M5UnitUnified.h>

#include "tap_types.hpp"

std::string uidHex(const uint8_t* bytes, size_t length);

class PresentationGate {
public:
    bool observe(const TagRead* seen);

private:
    std::string active_uid_;
    TagTechnology active_technology_ = TagTechnology::A;
    uint8_t misses_ = 0;
};

class NfcReader {
public:
    bool begin();
    bool poll(TagRead& out);

private:
    m5::unit::UnitUnified units_;
    m5::unit::UnitNFC reader_;
    m5::nfc::NFCLayerA layer_a_{reader_};
    m5::nfc::NFCLayerV layer_v_{reader_};
    PresentationGate gate_;
    bool ready_ = false;
};
