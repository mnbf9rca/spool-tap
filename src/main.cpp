#include <M5Unified.h>
#include <M5UnitUnified.h>
#include <M5UnitUnifiedNFC.h>
#include <wiring/m5_unit_unified_wiring.hpp>

namespace {
m5::unit::UnitUnified units;
m5::unit::UnitNFC reader;
bool reader_ready = false;
uint32_t last_heartbeat = 0;
}  // namespace

void setup() {
    Serial.begin(115200);
    M5.begin();

    auto config = reader.config();
    config.mode = m5::nfc::NFC::V;
    reader.config(config);
    reader_ready = m5::unit::wiring::addI2C(units, reader, 0, m5::unit::wiring::NessoPort::PortA) && units.begin();

    M5.Display.fillScreen(reader_ready ? TFT_DARKGREEN : TFT_RED);
    M5.Display.setTextSize(3);
    M5.Display.setCursor(0, 0);
    M5.Display.printf("Spool Tap\nNFC %s\n", reader_ready ? "ready" : "error");
    Serial.printf("SPOOL_TAP_BOOT NFC_READY=%d\n", reader_ready);
}

void loop() {
    M5.update();
    units.update();
    if (millis() - last_heartbeat >= 2000) {
        Serial.printf("SPOOL_TAP_LOOP NFC_READY=%d\n", reader_ready);
        last_heartbeat = millis();
    }
    delay(20);
}
