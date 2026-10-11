#pragma once

#include <WebServer.h>

#include "logs.hpp"
#include "ota.hpp"
#include "settings.hpp"
#include "ui.hpp"

class Web {
public:
    Web(Logs& logs, Settings& settings, Settings::Store& store, bool& settings_change_pending,
        Ota& ota, Ui& ui)
        : logs_(logs), settings_(settings), store_(store),
          settings_change_pending_(settings_change_pending), ota_(ota), ui_(ui) {}
    void begin();
    void tick(bool allow_settings_changes = true);
    bool rebootDue() const;

private:
    bool authorized();
    bool hasValidDigest();
    void showLogs();
    void showSettings();
    void saveSettings();
    void showUpdate();
    void uploadUpdate();
    void finishUpdate();
    void reportUpdateFailure();

    Logs& logs_;
    Settings& settings_;
    Settings::Store& store_;
    bool& settings_change_pending_;
    Ota& ota_;
    Ui& ui_;
    WebServer server_{80};
    bool started_ = false;
    bool allow_settings_changes_ = true;
    bool upload_allowed_ = false;
    bool upload_failure_reported_ = false;
    uint32_t reboot_at_ = 0;
    size_t last_progress_ = 0;
};
