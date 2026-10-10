#include <M5Unified.h>

#include "nfc_reader.hpp"
#include "tag_ndef.hpp"

namespace {
NfcReader reader;
bool reader_ready = false;
uint32_t last_heartbeat = 0;
}  // namespace

void setup() {
    Serial.begin(115200);
    M5.begin();

    reader_ready = reader.begin();

    M5.Display.fillScreen(reader_ready ? TFT_DARKGREEN : TFT_RED);
    M5.Display.setTextSize(3);
    M5.Display.setCursor(0, 0);
    M5.Display.printf("Spool Tap\nNFC %s\n", reader_ready ? "ready" : "error");
    Serial.printf("SPOOL_TAP_BOOT NFC_READY=%d\n", reader_ready);
}

void loop() {
    M5.update();
    TagRead tag;
    if (reader.poll(tag)) {
        Serial.printf("NFC_TAG UID=%s TECH=%c TYPE=%s CAPACITY=%u\n", tag.uid.c_str(),
                      tag.technology == TagTechnology::A ? 'A' : 'V', tag.type.c_str(), tag.capacity);
        if (!tag.memory.empty()) {
            std::vector<NdefRecord> records;
            const bool ndef_ok = extractNdef(tag, records);
            Serial.printf("NFC_NDEF_OK=%d RECORDS=%u\n", ndef_ok, records.size());
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
        M5.Display.printf("Spool Tap\n%s\n%s", tag.type.c_str(), tag.uid.c_str());
    }
    if (millis() - last_heartbeat >= 2000) {
        Serial.printf("SPOOL_TAP_LOOP NFC_READY=%d\n", reader_ready);
        last_heartbeat = millis();
    }
    delay(20);
}
