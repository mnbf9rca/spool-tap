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
