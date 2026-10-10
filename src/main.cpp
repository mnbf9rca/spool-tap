#include <M5Unified.h>

#include "nfc_reader.hpp"
#include "tag_ndef.hpp"
#include "tag_decode.hpp"
#include "net.hpp"
#include "services.hpp"
#include "settings.hpp"

namespace {
NfcReader reader;
Net net;
Settings settings;
NvsSettingsStore settings_store;
Services services(net, settings);
Mapping current_mapping;
SpoolInfo current_spool;
bool reader_ready = false;
uint32_t last_heartbeat = 0;
uint32_t last_net_status = 0;
enum class BootRead { Mapping, Spool, Done };
BootRead boot_read = BootRead::Mapping;
}  // namespace

void setup() {
    Serial.begin(115200);
    M5.begin();

    reader_ready = reader.begin();
    if (!settings_store.begin() || !settings.load(settings_store))
        Serial.println("SETTINGS_FAULT using defaults");
    net.begin();

    M5.Display.fillScreen(reader_ready ? TFT_DARKGREEN : TFT_RED);
    M5.Display.setTextSize(3);
    M5.Display.setCursor(0, 0);
    M5.Display.printf("Spool Tap\nNFC %s\n", reader_ready ? "ready" : "error");
    Serial.printf("SPOOL_TAP_BOOT NFC_READY=%d\n", reader_ready);
    Serial.println("NET_WAIT Wi-Fi and clock");
}

void loop() {
    M5.update();
    if (boot_read != BootRead::Done && millis() - last_net_status >= 2000) {
        ApiError error;
        if (net.ready(error)) {
            if (boot_read == BootRead::Mapping) {
                if (services.getMapping(current_mapping, error)) {
                    Serial.printf("BRIDGE_MAPPING SPOOL_ID=%u\n", current_mapping.spool_id);
                    boot_read = current_mapping.spool_id ? BootRead::Spool : BootRead::Done;
                } else Serial.printf("BRIDGE_ERROR %s\n", apiErrorLabel(error));
            } else if (services.getSpool(current_mapping.spool_id, current_spool, error)) {
                Serial.printf("SPOOLMAN_SPOOL ID=%u REMAINING_G=%.2f\n",
                              current_spool.id, current_spool.remaining_g);
                boot_read = BootRead::Done;
            } else Serial.printf("SPOOLMAN_ERROR %s\n", apiErrorLabel(error));
        } else {
            Serial.printf("NET_WAIT %s\n", apiErrorLabel(error));
        }
        last_net_status = millis();
    }
    TagRead tag;
    if (reader.poll(tag)) {
        Serial.printf("NFC_TAG UID=%s TECH=%c TYPE=%s CAPACITY=%u\n", tag.uid.c_str(),
                      tag.technology == TagTechnology::A ? 'A' : 'V', tag.type.c_str(), tag.capacity);
        TagHints hints;
        bool decoded = false;
        bool unreadable = false;
        if (!tag.memory.empty()) {
            std::vector<NdefRecord> records;
            const bool ndef_ok = extractNdef(tag, records);
            Serial.printf("NFC_NDEF_OK=%d RECORDS=%u\n", ndef_ok, records.size());
            decoded = ndef_ok && decodeTag(records, hints);
            unreadable = !ndef_ok || (!decoded && !records.empty());
#if defined(SPOOL_TAP_CAPTURE_NDEF)
            for (const auto& record : records) {
                if (record.type != "application/vnd.openprinttag" || record.payload.size() > 512) continue;
                Serial.print("V_NDEF_HEX=");
                for (uint8_t byte : record.payload) Serial.printf("%02X", byte);
                Serial.println();
            }
#endif
        }
        M5.Display.fillScreen(TFT_DARKGREEN);
        M5.Display.setCursor(0, 0);
        M5.Display.setTextSize(2);
        M5.Display.printf("Spool Tap\n%s\n%s\n", tag.type.c_str(), tag.uid.c_str());
        if (decoded) {
            M5.Display.printf("%s\n%s %s\n%s\n", hints.brand.c_str(),
                              hints.material_type.c_str(), hints.material.c_str(), hints.color_hex.c_str());
            if (!hints.identifier.empty()) M5.Display.printf("%s\n", hints.identifier.c_str());
            if (hints.nominal_weight_g > 0) M5.Display.printf("Nominal %.0fg\n", hints.nominal_weight_g);
            if (!hints.min_temp_c.empty() || !hints.max_temp_c.empty())
                M5.Display.printf("%s-%sC\n", hints.min_temp_c.c_str(), hints.max_temp_c.c_str());
        } else if (unreadable) {
            M5.Display.println("Tag data unreadable");
        }
    }
    if (millis() - last_heartbeat >= 2000) {
        Serial.printf("SPOOL_TAP_LOOP NFC_READY=%d\n", reader_ready);
        last_heartbeat = millis();
    }
    delay(20);
}
