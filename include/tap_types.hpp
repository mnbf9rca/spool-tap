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
