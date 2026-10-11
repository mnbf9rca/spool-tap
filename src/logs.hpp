#pragma once

#include <array>
#include <cstddef>
#include <ctime>
#include <string>

enum class LogLevel { Info, Warning, Error };
enum class LogEvent {
    Boot, ReaderReady, ReaderUnavailable, SettingsFallback, WifiConnected, WifiLost,
    MappingReady, MappingFailed, SpoolFailed, TagScanned, ScanFailed,
    LoadConfirmed, UnloadConfirmed, MappingWriteFailed, SettingsChanged
};

struct LogEntry {
    std::time_t when = 0;
    LogLevel level = LogLevel::Info;
    LogEvent event = LogEvent::Boot;
};

std::string escapeHtml(const std::string& text);

class Logs {
public:
    void add(LogLevel level, LogEvent message, std::time_t now = std::time(nullptr));
    std::string renderHtml() const;
    size_t size() const { return size_; }

private:
    std::array<LogEntry, 100> entries_{};
    size_t next_ = 0;
    size_t size_ = 0;
};
