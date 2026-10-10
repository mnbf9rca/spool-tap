#include "nfc_reader.hpp"

#include <Arduino.h>
#include <wiring/m5_unit_unified_wiring.hpp>

std::string uidHex(const uint8_t* bytes, size_t length) {
    if (!bytes || length == 0 || length > 10) return {};
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string result;
    result.reserve(length * 2);
    for (size_t i = 0; i < length; ++i) {
        result += hex[bytes[i] >> 4];
        result += hex[bytes[i] & 0x0f];
    }
    return result;
}

bool PresentationGate::observe(const TagRead* seen) {
    if (!seen) {
        if (misses_ < 2) ++misses_;
        if (misses_ == 2) active_uid_.clear();
        return false;
    }
    if (seen->uid.empty()) return false;
    misses_ = 0;
    if (seen->uid == active_uid_ && seen->technology == active_technology_) return false;
    active_uid_ = seen->uid;
    active_technology_ = seen->technology;
    return true;
}

bool NfcReader::begin() {
    auto config = reader_.config();
    config.mode = m5::nfc::NFC::V;
    reader_.config(config);
    ready_ = m5::unit::wiring::addI2C(units_, reader_, 0, m5::unit::wiring::NessoPort::PortA) && units_.begin();
    return ready_;
}

bool NfcReader::poll(TagRead& out) {
    if (!ready_) return false;
    units_.update();

    const bool a_ready = reader_.disableField() && reader_.configureNFCMode(m5::nfc::NFC::A);
    if (a_ready) {
        m5::nfc::a::PICC picc{};
        if (layer_a_.detect(picc, 50)) {
            TagRead seen;
            seen.uid = uidHex(picc.uid, picc.size);
            if (!gate_.observe(&seen)) return false;
            layer_a_.identify(picc);
            seen.type = picc.typeAsString();
            seen.capacity = picc.userAreaSize();
            out = seen;
            return true;
        }
    }

    const bool v_ready = reader_.disableField() && reader_.configureNFCMode(m5::nfc::NFC::V);
    if (v_ready) {
        m5::nfc::v::PICC picc{};
        if (layer_v_.detect(picc, 50)) {
            TagRead seen;
            seen.uid = uidHex(picc.uid, sizeof(picc.uid));
            seen.technology = TagTechnology::V;
            if (!gate_.observe(&seen)) return false;
            seen.type = picc.typeAsString();
            seen.capacity = picc.userAreaSize();
            out = seen;

            if (picc.valid() && seen.capacity <= 512 && layer_v_.reactivate(picc)) {
                m5::nfc::ndef::TLV message;
                const bool ok = layer_v_.ndefRead(message);
                Serial.printf("V_NDEF_OK=%d MESSAGE=%d\n", ok, ok && message.isMessageTLV());
                if (ok && message.isMessageTLV()) {
                    for (const auto& record : message.records()) {
                        if (std::string(record.type()) != "application/vnd.openprinttag" ||
                            record.payloadSize() > seen.capacity || record.payloadSize() > 512) continue;
                        Serial.print("V_NDEF_HEX=");
                        for (uint32_t i = 0; i < record.payloadSize(); ++i) {
                            Serial.printf("%02X", record.payload()[i]);
                        }
                        Serial.println();
                    }
                }
                layer_v_.deactivate();
            }
            return true;
        }
    }

    if (a_ready && v_ready) gate_.observe(nullptr);
    return false;
}
