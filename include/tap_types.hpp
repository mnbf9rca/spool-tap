#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

enum class TagTechnology : uint8_t { A, V };
constexpr size_t kMaxTagMemoryBytes = 1024;

struct TagRead {
    std::string uid;
    TagTechnology technology = TagTechnology::A;
    std::string type;
    std::string format;
    bool ndef_malformed = false;
    uint16_t capacity = 0;
    uint8_t block_size = 0;
    std::vector<uint8_t> memory;
};

struct TagHints {
    std::string format;
    std::string brand;
    std::string material;
    std::string material_type;
    std::string color_hex;
    std::string identifier;
    double nominal_weight_g = 0;
    std::string min_temp_c;
    std::string max_temp_c;
};

struct SettingsInput {
    std::string spoolman_url = "https://spoolman.cynexia.net";
    std::string filabridge_url = "https://filabridge.cynexia.net";
    std::string printer_name = "Core One";
    int32_t toolhead_id = 0;
    std::string reader_id = "spool-tap-cores3";
};

struct Mapping {
    uint32_t spool_id = 0;
    bool stale = false;
};

struct SpoolInfo {
    uint32_t id = 0;
    std::string vendor;
    std::string name;
    std::string material;
    std::string color_hex;
    double remaining_g = -1;
    bool archived = false;
};

enum class ScanKind { Unknown, FilamentOnly, Spool };

struct ScanMatch {
    ScanKind kind = ScanKind::Unknown;
    uint32_t spool_id = 0;
    uint32_t filament_id = 0;
    SpoolInfo spool;
};

enum class ApiError {
    None, Wifi, Time, Dns, Tls, Timeout, Http, TooLarge, InvalidUrl,
    Malformed, MissingSpool, ArchivedSpool, Conflict, InvalidTag
};
