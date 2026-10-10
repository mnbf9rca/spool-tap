#include <M5Unified.h>
#include <WiFi.h>

#include "nfc_reader.hpp"
#include "tag_ndef.hpp"
#include "tag_decode.hpp"
#include "net.hpp"
#include "services.hpp"
#include "settings.hpp"
#include "ui.hpp"

namespace {
NfcReader reader;
Net net;
Settings settings;
NvsSettingsStore settings_store;
Services services(net, settings);
Ui ui;
Mapping current_mapping;
SpoolInfo current_spool;
bool reader_ready = false;
bool bridge_ready = false;
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

    ui.setPrinterName(settings.current().printer_name);
    ui.setConnections(false, reader_ready, false);
    ui.showWorking();
    ui.render();
    Serial.printf("SPOOL_TAP_BOOT NFC_READY=%d\n", reader_ready);
    Serial.println("NET_WAIT Wi-Fi and clock");
}

void loop() {
    M5.update();
    ui.setConnections(WiFi.status() == WL_CONNECTED, reader_ready, bridge_ready);
    const UiAction action = ui.pollAction();
    if (action == UiAction::Status) {
        ui.showStatus(WiFi.localIP().toString().c_str(), "v1");
        ui.render();
    } else if (action == UiAction::Back || action == UiAction::Cancel) {
        if (boot_read == BootRead::Done)
            ui.showHome(current_mapping, current_spool.id == current_mapping.spool_id ? &current_spool : nullptr);
        else ui.showWorking();
        ui.render();
    }
    if (boot_read != BootRead::Done && millis() - last_net_status >= 2000) {
        ApiError error;
        if (net.ready(error)) {
            if (boot_read == BootRead::Mapping) {
                if (services.getMapping(current_mapping, error)) {
                    bridge_ready = true;
                    Serial.printf("BRIDGE_MAPPING SPOOL_ID=%u\n", current_mapping.spool_id);
                    boot_read = current_mapping.spool_id ? BootRead::Spool : BootRead::Done;
                    if (boot_read == BootRead::Done) {
                        ui.setConnections(true, reader_ready, bridge_ready);
                        ui.showHome(current_mapping, nullptr);
                        ui.render();
                    } else {
                        ui.setConnections(true, reader_ready, bridge_ready);
                        ui.showWorking();
                        ui.render();
                    }
                } else {
                    bridge_ready = false;
                    Serial.printf("BRIDGE_ERROR %s\n", apiErrorLabel(error));
                    ui.showError(error);
                    ui.render();
                }
            } else if (services.getSpool(current_mapping.spool_id, current_spool, error)) {
                Serial.printf("SPOOLMAN_SPOOL ID=%u REMAINING_G=%.2f\n",
                              current_spool.id, current_spool.remaining_g);
                boot_read = BootRead::Done;
                ui.setConnections(true, reader_ready, bridge_ready);
                ui.showHome(current_mapping, &current_spool);
                ui.render();
            } else {
                Serial.printf("SPOOLMAN_ERROR %s\n", apiErrorLabel(error));
                ui.showError(error);
                ui.render();
            }
        } else {
            Serial.printf("NET_WAIT %s\n", apiErrorLabel(error));
            ui.showError(error);
            ui.render();
        }
        last_net_status = millis();
    }
    TagRead tag;
    if (reader.poll(tag) && ui.view().screen == UiScreen::Home) {
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
        if (decoded) tag.format = hints.format;
        tag.ndef_malformed = unreadable;
        ui.showWorking();
        ui.render();
        ScanMatch match;
        ApiError scan_error;
        if (services.scan(tag, match, scan_error)) {
            Serial.printf("SCAN_MATCH KIND=%d SPOOL_ID=%u FILAMENT_ID=%u\n",
                          static_cast<int>(match.kind), match.spool_id, match.filament_id);
            ui.showTag(tag, hints, match, current_spool.id == current_mapping.spool_id ? &current_spool : nullptr);
        } else {
            Serial.printf("SCAN_ERROR %s\n", apiErrorLabel(scan_error));
            ui.showError(scan_error);
        }
        ui.render();
    }
    if (millis() - last_heartbeat >= 2000) {
        Serial.printf("SPOOL_TAP_LOOP NFC_READY=%d\n", reader_ready);
        last_heartbeat = millis();
    }
    delay(20);
}
