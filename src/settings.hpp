#pragma once

#include <Preferences.h>

#include "tap_types.hpp"

class Settings {
public:
    class Store {
    public:
        enum class ReadResult { Missing, Ok, Error };
        virtual ~Store() = default;
        virtual ReadResult read(std::vector<uint8_t>& bytes) = 0;
        virtual bool write(const std::vector<uint8_t>& bytes) = 0;
    };

    bool load(Store& store);
    bool save(Store& store, const SettingsInput& input);
    const SettingsInput& current() const { return current_; }
    std::string readerName() const { return current_.printer_name + " spool tap"; }
    bool fault() const { return fault_; }
    const std::string& error() const { return error_; }

private:
    SettingsInput current_;
    bool fault_ = false;
    std::string error_;
};

class NvsSettingsStore : public Settings::Store {
public:
    bool begin();
    ReadResult read(std::vector<uint8_t>& bytes) override;
    bool write(const std::vector<uint8_t>& bytes) override;
    ~NvsSettingsStore();

private:
    Preferences preferences_;
    bool ready_ = false;
};

bool sameDeviceOrigin(const std::string& origin, const std::string& host,
                      const std::string& device_ip);
bool parseToolheadId(const std::string& text, int32_t& value);
std::string renderSettingsPage(const Settings& settings, const std::string& message = "");
