#pragma once

#include "tap_ports.hpp"

class Logs;
class Settings;

class App {
public:
    App(ReaderPort& reader, ServicesPort& services, UiPort& ui, ClockPort& clock,
        Logs* logs = nullptr, Settings* settings = nullptr, bool* settings_change_pending = nullptr)
        : reader_(reader), services_(services), ui_(ui), clock_(clock), logs_(logs),
          settings_(settings), settings_change_pending_(settings_change_pending) {}

    void tick();
    void settingsChanged();
    bool bridgeReady() const { return bridge_ready_; }

private:
    bool refreshHome(uint32_t now);
    bool showMapping(const Mapping& mapping, uint32_t now);
    void confirm(UiAction operation, uint32_t now);

    ReaderPort& reader_;
    ServicesPort& services_;
    UiPort& ui_;
    ClockPort& clock_;
    Logs* logs_;
    Settings* settings_;
    bool* settings_change_pending_;
    Mapping mapping_;
    SpoolInfo current_spool_;
    TagRead candidate_tag_;
    TagHints candidate_hints_;
    ScanMatch candidate_;
    UiAction pending_ = UiAction::None;
    bool have_home_ = false;
    bool refresh_pending_ = true;
    bool attempted_ = false;
    bool bridge_ready_ = false;
    uint32_t last_attempt_ = 0;
    uint32_t last_refresh_ = 0;
};
