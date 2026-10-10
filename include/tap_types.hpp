#pragma once

#include <cstdint>
#include <string>

enum class TagTechnology : uint8_t { A, V };

struct TagRead {
    std::string uid;
    TagTechnology technology = TagTechnology::A;
    std::string type;
    uint16_t capacity = 0;
};
