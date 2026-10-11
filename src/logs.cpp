#include "logs.hpp"

#include <cstdio>

namespace {
constexpr std::time_t kEarliestValidTime = 1735689600; // 2025-01-01 UTC
const char* levelText(LogLevel level) {
    switch (level) {
    case LogLevel::Info: return "Info";
    case LogLevel::Warning: return "Warning";
    case LogLevel::Error: return "Error";
    }
    return "Error";
}

const char* eventText(LogEvent event) {
    switch (event) {
    case LogEvent::Boot: return "Device booted";
    case LogEvent::ReaderReady: return "NFC reader ready";
    case LogEvent::ReaderUnavailable: return "NFC reader unavailable";
    case LogEvent::SettingsFallback: return "Saved settings unavailable; defaults loaded";
    case LogEvent::WifiConnected: return "Wi-Fi connected";
    case LogEvent::WifiLost: return "Wi-Fi disconnected";
    case LogEvent::MappingReady: return "Mapping refreshed";
    case LogEvent::MappingFailed: return "Mapping refresh failed";
    case LogEvent::SpoolFailed: return "Spool details unavailable";
    case LogEvent::TagScanned: return "Tag scanned";
    case LogEvent::ScanFailed: return "Tag scan failed";
    case LogEvent::LoadConfirmed: return "Spool loaded";
    case LogEvent::UnloadConfirmed: return "Spool unloaded";
    case LogEvent::MappingWriteFailed: return "Mapping change failed";
    case LogEvent::SettingsChanged: return "Settings changed";
    }
    return "Unknown event";
}
}  // namespace

std::string escapeHtml(const std::string& text) {
    std::string escaped;
    escaped.reserve(text.size());
    for (char c : text) {
        switch (c) {
        case '&': escaped += "&amp;"; break;
        case '<': escaped += "&lt;"; break;
        case '>': escaped += "&gt;"; break;
        case '"': escaped += "&quot;"; break;
        case '\'': escaped += "&#39;"; break;
        default: escaped += c; break;
        }
    }
    return escaped;
}

void Logs::add(LogLevel level, LogEvent message, std::time_t now) {
    entries_[next_].when = now;
    entries_[next_].level = level;
    entries_[next_].event = message;
    next_ = (next_ + 1) % entries_.size();
    if (size_ < entries_.size()) ++size_;
}

std::string Logs::renderHtml() const {
    std::string html = "<!doctype html><html><head><meta charset=\"utf-8\"><title>Spool Tap logs</title>"
                       "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"></head>"
                       "<body><h1>Recent events</h1><ol>";
    for (size_t i = 0; i < size_; ++i) {
        const LogEntry& entry = entries_[(next_ + entries_.size() - size_ + i) % entries_.size()];
        char stamp[32];
        tm utc{};
        if (entry.when >= kEarliestValidTime && gmtime_r(&entry.when, &utc))
            strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S UTC", &utc);
        else std::snprintf(stamp, sizeof(stamp), "Clock not set");
        html += "<li><time>";
        html += escapeHtml(stamp);
        html += "</time> ";
        html += escapeHtml(levelText(entry.level));
        html += ": ";
        html += escapeHtml(eventText(entry.event));
        html += "</li>";
    }
    html += "</ol></body></html>";
    return html;
}
