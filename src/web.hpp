#pragma once

#include <WebServer.h>

#include "logs.hpp"
#include "settings.hpp"

class Web {
public:
    Web(Logs& logs, Settings& settings, Settings::Store& store, bool& settings_change_pending)
        : logs_(logs), settings_(settings), store_(store),
          settings_change_pending_(settings_change_pending) {}
    void begin();
    void tick(bool allow_settings_changes = true);

private:
    bool authorized();
    void showLogs();
    void showSettings();
    void saveSettings();

    Logs& logs_;
    Settings& settings_;
    Settings::Store& store_;
    bool& settings_change_pending_;
    WebServer server_{80};
    bool started_ = false;
    bool allow_settings_changes_ = true;
};
