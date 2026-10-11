#include "web.hpp"

#include <WiFi.h>

#ifndef SPOOL_TAP_UNIT_TEST
#include <build_secrets.hpp>
#endif

namespace {
constexpr char kUsername[] = "spool-tap";
constexpr char kRealm[] = "Spool Tap";
}

void Web::begin() {
    const char* headers[] = {"Origin"};
    server_.collectHeaders(headers, 1);
    server_.on("/logs", HTTP_GET, [this] { showLogs(); });
    server_.on("/settings", HTTP_GET, [this] { showSettings(); });
    server_.on("/settings", HTTP_POST, [this] { saveSettings(); });
    server_.begin();
    started_ = true;
}

void Web::tick(bool allow_settings_changes) {
    allow_settings_changes_ = allow_settings_changes;
    if (started_) server_.handleClient();
    allow_settings_changes_ = true;
}

bool Web::authorized() {
#ifndef SPOOL_TAP_UNIT_TEST
    const char* password = BuildSecrets::ota_password;
#else
    const char* password = "";
#endif
    // WebServer::authenticate also accepts Basic, so require a Digest header explicitly.
    if (!server_.header("Authorization").startsWith("Digest ") ||
        !server_.authenticate(kUsername, password)) {
        server_.sendHeader("Cache-Control", "no-store");
        server_.requestAuthentication(DIGEST_AUTH, kRealm);
        return false;
    }
    return true;
}

void Web::showLogs() {
    if (!authorized()) return;
    server_.sendHeader("Cache-Control", "no-store");
    server_.sendHeader("X-Content-Type-Options", "nosniff");
    server_.send(200, "text/html; charset=utf-8", logs_.renderHtml().c_str());
}

void Web::showSettings() {
    if (!authorized()) return;
    server_.sendHeader("Cache-Control", "no-store");
    server_.sendHeader("X-Content-Type-Options", "nosniff");
    server_.send(200, "text/html; charset=utf-8", renderSettingsPage(settings_).c_str());
}

void Web::saveSettings() {
    if (!authorized()) return;
    server_.sendHeader("Cache-Control", "no-store");
    server_.sendHeader("X-Content-Type-Options", "nosniff");
    if (!allow_settings_changes_) {
        server_.send(503, "text/plain; charset=utf-8", "Device busy; retry settings save");
        return;
    }
    if (!sameDeviceOrigin(server_.header("Origin").c_str(), server_.hostHeader().c_str(),
                          WiFi.localIP().toString().c_str())) {
        server_.send(403, "text/plain; charset=utf-8", "Invalid Origin");
        return;
    }
    if (!server_.hasArg("spoolman_url") || !server_.hasArg("filabridge_url") ||
        !server_.hasArg("printer_name") || !server_.hasArg("toolhead_id") ||
        !server_.hasArg("reader_id")) {
        server_.send(400, "text/html; charset=utf-8",
                     renderSettingsPage(settings_, "Missing setting").c_str());
        return;
    }
    SettingsInput input;
    input.spoolman_url = server_.arg("spoolman_url").c_str();
    input.filabridge_url = server_.arg("filabridge_url").c_str();
    input.printer_name = server_.arg("printer_name").c_str();
    input.reader_id = server_.arg("reader_id").c_str();
    if (!parseToolheadId(server_.arg("toolhead_id").c_str(), input.toolhead_id)) {
        server_.send(422, "text/html; charset=utf-8",
                     renderSettingsPage(settings_, "Invalid toolhead ID").c_str());
        return;
    }
    if (!settings_.save(store_, input)) {
        const int status = settings_.error() == "Settings storage failed" ? 500 : 422;
        server_.send(status, "text/html; charset=utf-8", renderSettingsPage(settings_).c_str());
        return;
    }
    settings_change_pending_ = true;
    server_.send(200, "text/html; charset=utf-8",
                 renderSettingsPage(settings_, "Settings saved").c_str());
}
