#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

constexpr unsigned kType5BlockRetries = 3;
constexpr unsigned kType5WholeReadRetries = 1;

template <typename ReadBlock, typename Reactivate>
bool readType5Memory(uint8_t* bytes, size_t length, uint8_t block_size,
                     ReadBlock read_block, Reactivate reactivate, uint16_t& failed_block) {
    if (!bytes || !length || !block_size || block_size > 32 || length % block_size ||
        length / block_size > UINT16_MAX) return false;
    for (unsigned read_attempt = 0; read_attempt <= kType5WholeReadRetries; ++read_attempt) {
        if (read_attempt && !reactivate()) return false;
        bool complete = true;
        for (size_t block = 0; block < length / block_size; ++block) {
            uint8_t data[32]{};
            bool read = false;
            for (unsigned retry = 0; retry <= kType5BlockRetries; ++retry) {
                if (read_block(data, static_cast<uint16_t>(block))) { read = true; break; }
            }
            if (!read) {
                failed_block = static_cast<uint16_t>(block);
                complete = false;
                break;
            }
            std::memcpy(bytes + block * block_size, data, block_size);
        }
        if (complete) { failed_block = UINT16_MAX; return true; }
    }
    return false;
}
