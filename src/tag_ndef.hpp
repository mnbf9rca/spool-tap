#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "tap_types.hpp"

struct NdefRecord {
    uint8_t tnf = 0;
    std::string type;
    std::vector<uint8_t> payload;
};

bool extractNdef(const TagRead& tag, std::vector<NdefRecord>& out);
