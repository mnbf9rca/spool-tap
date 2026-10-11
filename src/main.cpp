#include <M5Unified.h>
#include <WiFi.h>

#include "app.hpp"
#include "nfc_reader.hpp"
#include "tag_ndef.hpp"
#include "tag_decode.hpp"
#include "net.hpp"
#include "services.hpp"
#include "settings.hpp"
#include "ui.hpp"
#include "logs.hpp"
#include "web.hpp"
#include "ota.hpp"

namespace {
NfcReader reader;
Net net;
Settings settings;
NvsSettingsStore settings_store;
Services services(net, settings);
Ui ui;
Logs logs;
bool settings_change_pending = false;
ArduinoOtaPort ota_port;
Ota ota(ota_port);
Web web(logs, settings, settings_store, settings_change_pending, ota, ui);
bool reader_ready = false;
uint32_t last_heartbeat = 0;
bool boot_validated = false;

void pumpWeb() { web.tick(false); }

class DeviceReader : public ReaderPort {
public:
    explicit DeviceReader(NfcReader& reader) : reader_(reader) {}
    bool poll(TagRead& tag, TagHints& hints) override {
        if (!reader_.poll(tag)) return false;
        Serial.printf("NFC_TAG UID=%s TECH=%c TYPE=%s CAPACITY=%u\n", tag.uid.c_str(),
                      tag.technology == TagTechnology::A ? 'A' : 'V', tag.type.c_str(), tag.capacity);
        if (!tag.memory.empty()) {
            std::vector<NdefRecord> records;
            const bool ndef_ok = extractNdef(tag, records);
            Serial.printf("NFC_NDEF_OK=%d RECORDS=%u\n", ndef_ok, records.size());
            if (ndef_ok && decodeTag(records, hints)) tag.format = hints.format;
            tag.ndef_malformed = !ndef_ok;
#if defined(SPOOL_TAP_CAPTURE_NDEF)
            for (const auto& record : records) {
                if (record.type != "application/vnd.openprinttag" || record.payload.size() > 512) continue;
                Serial.print("V_NDEF_HEX=");
                for (uint8_t byte : record.payload) Serial.printf("%02X", byte);
                Serial.println();
            }
#endif
        }
        return true;
    }
private:
    NfcReader& reader_;
};

class DeviceServices : public ServicesPort {
public:
    explicit DeviceServices(Services& services) : services_(services) {}
    bool getMapping(Mapping& out, ApiError& error) override {
        const bool ok = services_.getMapping(out, error);
        web.tick(false);
        if (ok) Serial.printf("BRIDGE_MAPPING SPOOL_ID=%u\n", out.spool_id);
        else Serial.printf("BRIDGE_ERROR %s\n", apiErrorLabel(error));
        return ok;
    }
    bool getSpool(uint32_t id, SpoolInfo& out, ApiError& error) override {
        const bool ok = services_.getSpool(id, out, error);
        web.tick(false);
        if (ok) Serial.printf("SPOOLMAN_SPOOL ID=%u REMAINING_G=%.2f\n", out.id, out.remaining_g);
        else Serial.printf("SPOOLMAN_ERROR %s\n", apiErrorLabel(error));
        return ok;
    }
    bool scan(const TagRead& tag, ScanMatch& out, ApiError& error) override {
        const bool ok = services_.scan(tag, out, error);
        web.tick(false);
        if (ok) Serial.printf("SCAN_MATCH KIND=%d SPOOL_ID=%u FILAMENT_ID=%u\n",
                              static_cast<int>(out.kind), out.spool_id, out.filament_id);
        else Serial.printf("SCAN_ERROR %s\n", apiErrorLabel(error));
        return ok;
    }
    bool mapToolhead(uint32_t id, Mapping& after, ApiError& error) override {
        const bool ok = services_.mapToolhead(id, after, error);
        web.tick(false);
        Serial.printf("BRIDGE_MAP TARGET=%u RESULT=%s\n", id, ok ? "confirmed" : apiErrorLabel(error));
        return ok;
    }
private:
    Services& services_;
};

class DeviceUi : public UiPort {
public:
    explicit DeviceUi(Ui& ui) : ui_(ui) {}
    UiAction pollAction() override { return ui_.pollAction(); }
    Ui& ui() override { return ui_; }
    std::string ipAddress() const override { return WiFi.localIP().toString().c_str(); }
private:
    Ui& ui_;
};

class DeviceClock : public ClockPort {
public:
    uint32_t millis() const override { return ::millis(); }
};

DeviceReader app_reader(reader);
DeviceServices app_services(services);
DeviceUi app_ui(ui);
DeviceClock app_clock;
App app(app_reader, app_services, app_ui, app_clock, &logs,
        &settings_change_pending, &ota);

void updateConnections() {
    const bool wifi = WiFi.status() == WL_CONNECTED;
    if (ui.view().wifi != wifi)
        logs.add(wifi ? LogLevel::Info : LogLevel::Warning,
                 wifi ? LogEvent::WifiConnected : LogEvent::WifiLost);
    const bool bridge = wifi && app.bridgeReady() && !ui.view().stale;
    if (ui.view().wifi == wifi && ui.view().nfc == reader_ready &&
        ui.view().bridge == bridge) return;
    ui.setConnections(wifi, reader_ready, wifi && app.bridgeReady());
    ui.render();
}
}  // namespace

void setup() {
    Serial.begin(115200);
    M5.begin();
    logs.add(LogLevel::Info, LogEvent::Boot);
    reader_ready = reader.begin();
    logs.add(reader_ready ? LogLevel::Info : LogLevel::Warning,
             reader_ready ? LogEvent::ReaderReady : LogEvent::ReaderUnavailable);
    if (!settings_store.begin() || !settings.load(settings_store)) {
        Serial.println("SETTINGS_FAULT using defaults");
        logs.add(LogLevel::Warning, LogEvent::SettingsFallback);
    }
    net.begin();
    web.begin();
    services.setBetweenRequests(pumpWeb);
    ui.setConnections(false, reader_ready, false);
    ui.showWorking("Connecting Wi-Fi");
    ui.render();
    Serial.printf("SPOOL_TAP_BOOT NFC_READY=%d\n", reader_ready);
    Serial.printf("SPOOL_TAP_VERSION=%s\n", SPOOL_TAP_VERSION);
    Serial.println("NET_WAIT Wi-Fi and clock");
}

void loop() {
    M5.update();
    web.tick();
    updateConnections();
    app.tick();
    web.tick();
    updateConnections();
    if (!boot_validated && ota.validateHealthyLoop()) {
        boot_validated = true;
    }
    if (web.rebootDue()) ESP.restart();
    if (millis() - last_heartbeat >= 2000) {
        Serial.printf("SPOOL_TAP_LOOP NFC_READY=%d\n", reader_ready);
        last_heartbeat = millis();
    }
    delay(20);
}
