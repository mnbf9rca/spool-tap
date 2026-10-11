#include <cassert>
#include <cstdint>
#include <cstring>

#include "type5_read.hpp"

int main() {
    uint8_t bytes[12]{};
    uint16_t failed_block = UINT16_MAX;
    int block_one_calls = 0;
    int restarts = 0;
    bool ok = readType5Memory(bytes, sizeof(bytes), 4,
        [&](uint8_t* out, uint16_t block) {
            if (block == 1 && ++block_one_calls <= 3) return false;
            std::memset(out, block + 1, 4);
            return true;
        },
        [&] { ++restarts; return true; }, failed_block);
    assert(ok && block_one_calls == 4 && restarts == 0);
    assert(bytes[0] == 1 && bytes[4] == 2 && bytes[8] == 3);

    int calls = 0;
    restarts = 0;
    ok = readType5Memory(bytes, sizeof(bytes), 4,
        [&](uint8_t* out, uint16_t block) {
            if (block == 1 && calls++ < 4) return false;
            std::memset(out, block + 1, 4);
            return true;
        },
        [&] { ++restarts; return true; }, failed_block);
    assert(ok && restarts == 1 && calls == 5);

    calls = 0;
    restarts = 0;
    ok = readType5Memory(bytes, sizeof(bytes), 4,
        [&](uint8_t*, uint16_t block) {
            if (block == 1) { ++calls; return false; }
            return true;
        },
        [&] { ++restarts; return true; }, failed_block);
    assert(!ok && restarts == 1 && calls == 8 && failed_block == 1);

    calls = 0;
    ok = readType5Memory(bytes, sizeof(bytes), 5,
        [&](uint8_t*, uint16_t) { ++calls; return true; },
        [] { return true; }, failed_block);
    assert(!ok && calls == 0);
}
