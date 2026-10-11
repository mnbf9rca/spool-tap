#include "tag_ndef.hpp"

#include <algorithm>

namespace {
bool parseRecords(const uint8_t* bytes, size_t length, std::vector<NdefRecord>& out) {
    size_t offset = 0;
    bool first = true;
    while (offset < length) {
        if (length - offset < 3) return false;
        const uint8_t header = bytes[offset++];
        const uint8_t type_length = bytes[offset++];
        const bool short_record = header & 0x10;
        const bool has_id = header & 0x08;
        if ((header & 0x20) || bool(header & 0x80) != first) return false;

        uint32_t payload_length = 0;
        if (short_record) {
            payload_length = bytes[offset++];
        } else {
            if (length - offset < 4) return false;
            for (int i = 0; i < 4; ++i) payload_length = (payload_length << 8) | bytes[offset++];
        }
        uint8_t id_length = 0;
        if (has_id) {
            if (offset == length) return false;
            id_length = bytes[offset++];
        }
        const size_t remaining = length - offset;
        if (type_length > remaining || id_length > remaining - type_length ||
            payload_length > remaining - type_length - id_length) return false;

        NdefRecord record;
        record.tnf = header & 0x07;
        record.type.assign(reinterpret_cast<const char*>(bytes + offset), type_length);
        offset += type_length + id_length;
        record.payload.assign(bytes + offset, bytes + offset + payload_length);
        offset += payload_length;
        out.push_back(std::move(record));
        if (header & 0x40) return offset == length;
        first = false;
    }
    return false;
}
}  // namespace

bool extractNdef(const TagRead& tag, std::vector<NdefRecord>& out) {
    out.clear();
    const auto& bytes = tag.memory;
    if (bytes.size() < 4 || tag.capacity < 8 || bytes.size() > kMaxTagMemoryBytes ||
        tag.block_size == 0 || tag.block_size > 32 ||
        tag.capacity % tag.block_size || bytes.size() % tag.block_size || bytes[0] != 0xE1) return false;

    const bool type2 = tag.technology == TagTechnology::A;
    if ((bytes[1] & 0xF0) != (type2 ? 0x10 : 0x40)) return false;
    const size_t cc_capacity = static_cast<size_t>(bytes[2]) * 8;
    if (cc_capacity < 8 || cc_capacity > tag.capacity ||
        bytes.size() > static_cast<size_t>(tag.capacity) + (type2 ? 4 : 0)) return false;
    const size_t logical_end = cc_capacity + (type2 ? 4 : 0);
    const size_t available_end = std::min(logical_end, bytes.size());

    for (size_t offset = 4; offset < available_end;) {
        const uint8_t tlv_type = bytes[offset++];
        if (tlv_type == 0x00) continue;
        if (tlv_type == 0xFE) break;
        if (offset == available_end) return false;
        size_t value_length = bytes[offset++];
        if (value_length == 0xFF) {
            if (available_end - offset < 2) return false;
            value_length = (static_cast<size_t>(bytes[offset]) << 8) | bytes[offset + 1];
            offset += 2;
        }
        if (value_length > logical_end - offset || value_length > available_end - offset) return false;
        if (tlv_type == 0x03) {
            std::vector<NdefRecord> parsed;
            if (!parseRecords(bytes.data() + offset, value_length, parsed)) return false;
            out.swap(parsed);
            return true;
        }
        offset += value_length;
    }
    return false;
}
