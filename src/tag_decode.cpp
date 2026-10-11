#include "tag_decode.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

#include <cJSON.h>
#include "third_party/tinycbor/cbor.h"

namespace {
constexpr size_t kMaxPayload = 512;

bool unsignedValue(const CborValue& value, uint64_t& out) {
    return cbor_value_is_unsigned_integer(&value) &&
           cbor_value_get_uint64(&value, &out) == CborNoError;
}

bool textValue(const CborValue& value, size_t max, std::string& out) {
    if (!cbor_value_is_text_string(&value)) return false;
    size_t length = 0;
    if (cbor_value_calculate_string_length(&value, &length) != CborNoError || length > max) return false;
    char buffer[65] = {};
    size_t capacity = sizeof(buffer);
    if (cbor_value_copy_text_string(&value, buffer, &capacity, nullptr) != CborNoError) return false;
    out.assign(buffer, length);
    return true;
}

bool colorValue(const CborValue& value, std::string& out) {
    if (!cbor_value_is_byte_string(&value)) return false;
    size_t length = 0;
    if (cbor_value_calculate_string_length(&value, &length) != CborNoError ||
        (length != 3 && length != 4)) return false;
    uint8_t rgb[4];
    size_t capacity = sizeof(rgb);
    if (cbor_value_copy_byte_string(&value, rgb, &capacity, nullptr) != CborNoError) return false;
    char hex[7];
    snprintf(hex, sizeof(hex), "%02X%02X%02X", rgb[0], rgb[1], rgb[2]);
    out = hex;
    return true;
}

bool weightValue(const CborValue& value, double& out) {
    uint64_t integer = 0;
    if (unsignedValue(value, integer)) out = static_cast<double>(integer);
    else if (cbor_value_is_half_float(&value)) {
        uint16_t half = 0;
        if (cbor_value_get_half_float(&value, &half) != CborNoError) return false;
        const int exponent = (half >> 10) & 31;
        const int fraction = half & 1023;
        if (exponent == 31) return false;
        out = std::ldexp(exponent ? 1024 + fraction : fraction,
                         exponent ? exponent - 25 : -24);
        if (half & 0x8000) out = -out;
    } else if (cbor_value_is_float(&value)) {
        float number = 0;
        if (cbor_value_get_float(&value, &number) != CborNoError) return false;
        out = number;
    } else if (cbor_value_is_double(&value)) {
        if (cbor_value_get_double(&value, &out) != CborNoError) return false;
    } else return false;
    return std::isfinite(out) && out >= 0 && out <= 100000;
}

const char* materialType(uint64_t key) {
    switch (key) {
        case 0: return "PLA";
        case 1: return "PETG";
        case 2: return "TPU";
        case 3: return "ABS";
        case 4: return "ASA";
        case 5: return "PC";
        case 6: return "PCTG";
        default: return nullptr;
    }
}

bool mapFields(const uint8_t* bytes, size_t size, TagHints* hints,
               size_t& consumed, size_t* main_offset, size_t* main_size,
               size_t* aux_offset, size_t* aux_size) {
    CborParser parser;
    CborValue root;
    if (size == 0 || cbor_parser_init(bytes, size, 0, &parser, &root) != CborNoError ||
        !cbor_value_is_map(&root)) return false;
    CborValue field;
    if (cbor_value_enter_container(&root, &field) != CborNoError) return false;
    while (!cbor_value_at_end(&field)) {
        uint64_t key = UINT64_MAX;
        unsignedValue(field, key);
        if (cbor_value_advance(&field) != CborNoError || cbor_value_at_end(&field)) return false;
        uint64_t number = 0;
        if (hints) {
            switch (key) {
                case 0: {
                    if (!cbor_value_is_byte_string(&field)) return false;
                    size_t length = 0;
                    if (cbor_value_calculate_string_length(&field, &length) != CborNoError || length != 16) return false;
                    uint8_t id[16];
                    size_t capacity = sizeof(id);
                    if (cbor_value_copy_byte_string(&field, id, &capacity, nullptr) != CborNoError) return false;
                    char hex[33];
                    for (size_t i = 0; i < 16; ++i) snprintf(hex + i * 2, 3, "%02X", id[i]);
                    if (hints->identifier.empty()) hints->identifier = hex;
                    break;
                }
                case 4:
                    if (!unsignedValue(field, number)) return false;
                    hints->identifier = std::to_string(number);
                    break;
                case 9:
                    if (!unsignedValue(field, number)) return false;
                    if (const char* name = materialType(number)) hints->material_type = name;
                    break;
                case 10:
                    if (!textValue(field, 63, hints->material)) return false;
                    break;
                case 11:
                    if (!textValue(field, 31, hints->brand)) return false;
                    break;
                case 16:
                    if (!weightValue(field, hints->nominal_weight_g)) return false;
                    break;
                case 19:
                    if (!colorValue(field, hints->color_hex)) return false;
                    break;
            }
        } else {
            if (key == 0 && main_offset) {
                if (!unsignedValue(field, number) || number > size) return false;
                *main_offset = number;
            } else if (key == 1 && main_size) {
                if (!unsignedValue(field, number) || number > size) return false;
                *main_size = number;
            } else if (key == 2 && aux_offset) {
                if (!unsignedValue(field, number) || number > size) return false;
                *aux_offset = number;
            } else if (key == 3 && aux_size) {
                if (!unsignedValue(field, number) || number > size) return false;
                *aux_size = number;
            }
        }
        if (cbor_value_advance(&field) != CborNoError) return false;
    }
    if (cbor_value_leave_container(&root, &field) != CborNoError) return false;
    consumed = cbor_value_get_next_byte(&root) - bytes;
    return true;
}

bool decodePrintTag(const NdefRecord& record, TagHints& hints) {
    const auto& payload = record.payload;
    if (payload.empty() || payload.size() > kMaxPayload) return false;
    size_t meta_end = 0, main_offset = SIZE_MAX, main_size = SIZE_MAX;
    size_t aux_offset = SIZE_MAX, aux_size = SIZE_MAX;
    if (!mapFields(payload.data(), payload.size(), nullptr, meta_end,
                   &main_offset, &main_size, &aux_offset, &aux_size)) return false;
    if (main_offset == SIZE_MAX) main_offset = meta_end;
    if (main_offset < meta_end || main_offset >= payload.size() ||
        (aux_offset != SIZE_MAX && aux_offset < main_offset)) return false;
    const size_t main_end = main_size == SIZE_MAX ?
        (aux_offset == SIZE_MAX ? payload.size() : aux_offset) : main_offset + main_size;
    if (main_end > payload.size() || main_end <= main_offset ||
        (aux_offset != SIZE_MAX && main_end > aux_offset)) return false;
    size_t consumed = 0;
    if (!mapFields(payload.data() + main_offset, main_end - main_offset, &hints,
                   consumed, nullptr, nullptr, nullptr, nullptr)) return false;
    if (aux_offset != SIZE_MAX) {
        if (aux_offset > payload.size()) return false;
        const size_t end = aux_size == SIZE_MAX ? payload.size() : aux_offset + aux_size;
        if (end > payload.size()) return false;
        if (end != aux_offset && !mapFields(payload.data() + aux_offset, end - aux_offset,
                                           nullptr, consumed, nullptr, nullptr, nullptr, nullptr)) return false;
    }
    hints.format = "OpenPrintTag";
    return true;
}

bool jsonString(const cJSON* object, const char* key, size_t max, std::string& out) {
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!item) return true;
    if (!cJSON_IsString(item) || !item->valuestring || strlen(item->valuestring) > max) return false;
    out = item->valuestring;
    return true;
}

bool boundedJsonDepth(const std::vector<uint8_t>& bytes) {
    int depth = 0;
    bool quoted = false, escaped = false;
    for (uint8_t byte : bytes) {
        if (escaped) { escaped = false; continue; }
        if (quoted && byte == '\\') { escaped = true; continue; }
        if (byte == '"') { quoted = !quoted; continue; }
        if (quoted) continue;
        if (byte == '{' || byte == '[') {
            if (++depth > 16) return false;
        } else if (byte == '}' || byte == ']') {
            if (--depth < 0) return false;
        }
    }
    return !quoted && depth == 0;
}

bool decodeSpool(const NdefRecord& record, TagHints& hints) {
    if (record.payload.empty() || record.payload.size() > kMaxPayload ||
        memchr(record.payload.data(), 0, record.payload.size()) ||
        !boundedJsonDepth(record.payload)) return false;
    std::string json(record.payload.begin(), record.payload.end());
    const char* end = nullptr;
    cJSON* root = cJSON_ParseWithLengthOpts(json.c_str(), json.size() + 1, &end, true);
    if (!root) return false;
    const cJSON* protocol = cJSON_GetObjectItemCaseSensitive(root, "protocol");
    const cJSON* version = cJSON_GetObjectItemCaseSensitive(root, "version");
    bool ok = cJSON_IsObject(root) && cJSON_IsString(protocol) && cJSON_IsString(version) &&
              strcmp(protocol->valuestring, "openspool") == 0 &&
              strcmp(version->valuestring, "1.0") == 0 &&
              jsonString(root, "brand", 31, hints.brand) &&
              jsonString(root, "type", 31, hints.material_type) &&
              jsonString(root, "color_hex", 8, hints.color_hex) &&
              jsonString(root, "min_temp", 8, hints.min_temp_c) &&
              jsonString(root, "max_temp", 8, hints.max_temp_c);
    if (ok && !hints.color_hex.empty()) {
        ok = hints.color_hex.size() == 6;
        for (char ch : hints.color_hex) {
            if (!((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'F') ||
                  (ch >= 'a' && ch <= 'f'))) ok = false;
        }
    }
    cJSON_Delete(root);
    if (ok) hints.format = "OpenSpool";
    return ok;
}
}  // namespace

bool decodeTag(const std::vector<NdefRecord>& records, TagHints& out) {
    out = {};
    for (const auto& record : records) {
        if (record.tnf != 2) continue;
        TagHints hints;
        bool ok = false;
        if (record.type == "application/vnd.openprinttag") {
            ok = decodePrintTag(record, hints);
        } else if (record.type == "application/json") {
            ok = decodeSpool(record, hints);
        }
        if (ok) {
            out = std::move(hints);
            return true;
        }
    }
    return false;
}
