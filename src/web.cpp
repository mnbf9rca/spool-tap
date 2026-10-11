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
    server_.on("/update", HTTP_GET, [this] { showUpdate(); });
    server_.on("/update", HTTP_POST, [this] { finishUpdate(); }, [this] { uploadUpdate(); });
    server_.begin();
    started_ = true;
}

void Web::tick(bool allow_settings_changes) {
    allow_settings_changes_ = allow_settings_changes;
    if (started_) server_.handleClient();
    allow_settings_changes_ = true;
}

bool Web::hasValidDigest() {
#ifndef SPOOL_TAP_UNIT_TEST
    const char* password = BuildSecrets::ota_password;
#else
    const char* password = "";
#endif
    // WebServer::authenticate also accepts Basic, so require a Digest header explicitly.
    return server_.header("Authorization").startsWith("Digest ") &&
           server_.authenticate(kUsername, password);
}

bool Web::authorized() {
    if (!hasValidDigest()) {
        server_.sendHeader("Cache-Control", "no-store");
        server_.requestAuthentication(DIGEST_AUTH, kRealm);
        return false;
    }
    return true;
}

bool Web::rebootDue() const {
    return reboot_at_ && static_cast<int32_t>(millis() - reboot_at_) >= 0;
}

void Web::showUpdate() {
    if (!authorized()) return;
    server_.sendHeader("Cache-Control", "no-store");
    server_.sendHeader("X-Content-Type-Options", "nosniff");
    server_.send(200, "text/html; charset=utf-8",
                 "<!doctype html><html><head><meta charset=\"utf-8\"><title>Update Spool Tap</title>"
                 "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"></head>"
                 "<body><h1>Update Spool Tap</h1><p>Select a complete application firmware.bin. "
                 "Keep the device powered until it reboots.</p><form method=\"post\" "
                 "enctype=\"multipart/form-data\"><label>Firmware <input type=\"file\" "
                 "name=\"firmware\" required></label><button type=\"submit\">Upload</button>"
                 "</form></body></html>");
}

void Web::uploadUpdate() {
    HTTPUpload& upload = server_.upload();
    if (upload.status == UPLOAD_FILE_START) {
        upload_failure_reported_ = false;
        upload_allowed_ = hasValidDigest() && allow_settings_changes_ && !reboot_at_ &&
                          upload.name == "firmware" &&
                          (server_.header("Origin").isEmpty() ||
                           sameDeviceOrigin(server_.header("Origin").c_str(),
                                            server_.hostHeader().c_str(),
                                            WiFi.localIP().toString().c_str()));
        if (!upload_allowed_) {
            if (ota_.staged()) ota_.handleUpload(UploadStage::Abort);
            return;
        }
        upload_allowed_ = ota_.handleUpload(UploadStage::Start) == OtaStatus::Receiving;
        if (upload_allowed_) {
            logs_.add(LogLevel::Info, LogEvent::UpdateStarted);
            last_progress_ = 0;
            ui_.showUpdateProgress(0);
            ui_.render();
        }
        return;
    }
    if (!upload_allowed_) return;
    if (upload.status == UPLOAD_FILE_WRITE) {
        ota_.handleUpload(UploadStage::Write, upload.buf, upload.currentSize);
        if (ota_.busy() && ota_.bytesWritten() - last_progress_ >= 65536) {
            last_progress_ = ota_.bytesWritten();
            ui_.showUpdateProgress(last_progress_);
            ui_.render();
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        ota_.handleUpload(UploadStage::End, nullptr, 0, upload.totalSize);
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
        ota_.handleUpload(UploadStage::Abort);
    }
    if (ota_.failed()) {
        reportUpdateFailure();
        upload_allowed_ = false;
    }
}

void Web::reportUpdateFailure() {
    if (upload_failure_reported_) return;
    upload_failure_reported_ = true;
    logs_.add(LogLevel::Error, LogEvent::UpdateFailed);
    ui_.showUpdateResult(false);
    ui_.render();
}

void Web::finishUpdate() {
    if (!authorized()) return;
    server_.sendHeader("Cache-Control", "no-store");
    server_.sendHeader("X-Content-Type-Options", "nosniff");
    if (!allow_settings_changes_) {
        server_.send(503, "text/plain; charset=utf-8", "Device busy; retry update");
        return;
    }
    if (!server_.header("Origin").isEmpty() &&
        !sameDeviceOrigin(server_.header("Origin").c_str(), server_.hostHeader().c_str(),
                          WiFi.localIP().toString().c_str())) {
        server_.send(403, "text/plain; charset=utf-8", "Invalid Origin");
        return;
    }
    if (!upload_allowed_ || !ota_.staged() || ota_.commit() != OtaStatus::Ready) {
        reportUpdateFailure();
        server_.send(400, "text/plain; charset=utf-8", "Invalid or incomplete firmware image");
        upload_allowed_ = false;
        return;
    }
    upload_allowed_ = false;
    logs_.add(LogLevel::Info, LogEvent::UpdateReady);
    ui_.showUpdateResult(true);
    ui_.render();
    server_.send(200, "text/plain; charset=utf-8", "Update accepted; rebooting");
    reboot_at_ = millis() + 500;
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
