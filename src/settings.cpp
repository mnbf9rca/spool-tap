#include "settings.hpp"

#include <cJSON.h>

#include <climits>
#include <cstring>
#include <utility>

#include "logs.hpp"

namespace {
constexpr size_t kMaxRecordBytes = 1024;
constexpr uint8_t kRecordVersion = 1;

bool originValid(const std::string& url) {
    if (url.size() <= 8 || url.size() > 255 || url.compare(0, 8, "https://") != 0) return false;
    const std::string authority = url.substr(8);
    if (authority.find_first_of("/@?#\\ ") != std::string::npos) return false;
    size_t colon = authority.find(':');
    if (colon != std::string::npos) {
        if (authority.find(':', colon + 1) != std::string::npos || colon + 1 == authority.size()) return false;
        unsigned port = 0;
        for (size_t i = colon + 1; i < authority.size(); ++i) {
            const char ch = authority[i];
            if (ch < '0' || ch > '9') return false;
            port = port * 10 + ch - '0';
            if (port > 65535) return false;
        }
        if (port == 0) return false;
    } else colon = authority.size();
    const std::string host = authority.substr(0, colon);
    if (host.empty() || host.size() > 253) return false;
    size_t label_start = 0;
    for (size_t i = 0; i <= host.size(); ++i) {
        if (i == host.size() || host[i] == '.') {
            const size_t length = i - label_start;
            if (length == 0 || length > 63 || host[label_start] == '-' || host[i - 1] == '-') return false;
            label_start = i + 1;
        } else {
            const char ch = host[i];
            if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                  (ch >= '0' && ch <= '9') || ch == '-')) return false;
        }
    }
    return true;
}

bool validUtf8Name(const std::string& name) {
    if (name.empty() || name.size() > 54) return false;
    for (size_t i = 0; i < name.size();) {
        const uint8_t first = static_cast<uint8_t>(name[i++]);
        if (first < 0x20 || first == 0x7F) return false;
        if (first < 0x80) continue;
        uint32_t codepoint = 0, minimum = 0;
        int continuation = 0;
        if (first >= 0xC2 && first <= 0xDF) { codepoint = first & 0x1F; minimum = 0x80; continuation = 1; }
        else if (first >= 0xE0 && first <= 0xEF) { codepoint = first & 0x0F; minimum = 0x800; continuation = 2; }
        else if (first >= 0xF0 && first <= 0xF4) { codepoint = first & 0x07; minimum = 0x10000; continuation = 3; }
        else return false;
        if (i + continuation > name.size()) return false;
        while (continuation--) {
            const uint8_t byte = static_cast<uint8_t>(name[i++]);
            if ((byte & 0xC0) != 0x80) return false;
            codepoint = (codepoint << 6) | (byte & 0x3F);
        }
        if (codepoint < minimum || (codepoint >= 0xD800 && codepoint <= 0xDFFF) ||
            codepoint > 0x10FFFF) return false;
    }
    return true;
}

bool readerIdValid(const std::string& id) {
    if (id.empty() || id.size() > 64) return false;
    for (char ch : id) {
        if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
              (ch >= '0' && ch <= '9') || ch == '.' || ch == '_' ||
              ch == ':' || ch == '-')) return false;
    }
    return true;
}

bool serialize(const SettingsInput& input, std::vector<uint8_t>& bytes) {
    cJSON* root = cJSON_CreateObject();
    if (!root) return false;
    bool ok = cJSON_AddNumberToObject(root, "v", kRecordVersion) &&
              cJSON_AddStringToObject(root, "spoolman", input.spoolman_url.c_str()) &&
              cJSON_AddStringToObject(root, "filabridge", input.filabridge_url.c_str()) &&
              cJSON_AddStringToObject(root, "printer", input.printer_name.c_str()) &&
              cJSON_AddNumberToObject(root, "toolhead", input.toolhead_id) &&
              cJSON_AddStringToObject(root, "reader", input.reader_id.c_str());
    char* encoded = ok ? cJSON_PrintUnformatted(root) : nullptr;
    cJSON_Delete(root);
    if (!encoded) return false;
    const size_t length = strlen(encoded);
    ok = length > 0 && length <= kMaxRecordBytes;
    if (ok) bytes.assign(encoded, encoded + length);
    cJSON_free(encoded);
    return ok;
}

bool decode(const std::vector<uint8_t>& bytes, SettingsInput& input) {
    if (bytes.empty() || bytes.size() > kMaxRecordBytes ||
        memchr(bytes.data(), 0, bytes.size())) return false;
    std::string text(bytes.begin(), bytes.end());
    cJSON* root = cJSON_ParseWithLengthOpts(text.c_str(), text.size() + 1, nullptr, true);
    if (!root) return false;
    const cJSON* version = cJSON_GetObjectItemCaseSensitive(root, "v");
    const cJSON* spoolman = cJSON_GetObjectItemCaseSensitive(root, "spoolman");
    const cJSON* filabridge = cJSON_GetObjectItemCaseSensitive(root, "filabridge");
    const cJSON* printer = cJSON_GetObjectItemCaseSensitive(root, "printer");
    const cJSON* toolhead = cJSON_GetObjectItemCaseSensitive(root, "toolhead");
    const cJSON* reader = cJSON_GetObjectItemCaseSensitive(root, "reader");
    const bool ok = cJSON_IsObject(root) && cJSON_IsNumber(version) &&
                    version->valuedouble == kRecordVersion &&
                    cJSON_IsString(spoolman) && cJSON_IsString(filabridge) &&
                    cJSON_IsString(printer) && cJSON_IsNumber(toolhead) &&
                    cJSON_IsString(reader) && toolhead->valuedouble >= 0 &&
                    toolhead->valuedouble <= INT32_MAX &&
                    toolhead->valuedouble == static_cast<int32_t>(toolhead->valuedouble);
    if (ok) {
        input.spoolman_url = spoolman->valuestring;
        input.filabridge_url = filabridge->valuestring;
        input.printer_name = printer->valuestring;
        input.toolhead_id = static_cast<int32_t>(toolhead->valuedouble);
        input.reader_id = reader->valuestring;
    }
    cJSON_Delete(root);
    return ok;
}
}  // namespace

bool Settings::load(Store& store) {
    current_ = SettingsInput{};
    fault_ = false;
    error_.clear();
    std::vector<uint8_t> bytes;
    const Store::ReadResult result = store.read(bytes);
    if (result == Store::ReadResult::Missing) return true;
    SettingsInput candidate;
    if (result == Store::ReadResult::Ok && decode(bytes, candidate) &&
        originValid(candidate.spoolman_url) && originValid(candidate.filabridge_url) &&
        validUtf8Name(candidate.printer_name) && candidate.toolhead_id >= 0 &&
        readerIdValid(candidate.reader_id)) {
        current_ = std::move(candidate);
        return true;
    }
    fault_ = true;
    error_ = "Stored settings invalid or unreadable";
    return false;
}

bool Settings::save(Store& store, const SettingsInput& input) {
    error_.clear();
    if (!originValid(input.spoolman_url)) error_ = "Invalid Spoolman HTTPS origin";
    else if (!originValid(input.filabridge_url)) error_ = "Invalid FilaBridge HTTPS origin";
    else if (!validUtf8Name(input.printer_name)) error_ = "Invalid printer name";
    else if (input.toolhead_id < 0) error_ = "Invalid toolhead ID";
    else if (!readerIdValid(input.reader_id)) error_ = "Invalid reader ID";
    if (!error_.empty()) return false;
    std::vector<uint8_t> bytes;
    if (!serialize(input, bytes) || !store.write(bytes)) {
        error_ = "Settings storage failed";
        return false;
    }
    current_ = input;
    fault_ = false;
    return true;
}

bool NvsSettingsStore::begin() {
    if (ready_) return true;
    ready_ = preferences_.begin("spooltap", false);
    return ready_;
}

Settings::Store::ReadResult NvsSettingsStore::read(std::vector<uint8_t>& bytes) {
    bytes.clear();
    if (!ready_) return ReadResult::Error;
    if (!preferences_.isKey("settings")) return ReadResult::Missing;
    const size_t length = preferences_.getBytesLength("settings");
    if (length == 0 || length > kMaxRecordBytes) return ReadResult::Error;
    bytes.resize(length);
    if (preferences_.getBytes("settings", bytes.data(), length) != length) {
        bytes.clear();
        return ReadResult::Error;
    }
    return ReadResult::Ok;
}

bool NvsSettingsStore::write(const std::vector<uint8_t>& bytes) {
    return ready_ && !bytes.empty() && bytes.size() <= kMaxRecordBytes &&
           preferences_.putBytes("settings", bytes.data(), bytes.size()) == bytes.size();
}

NvsSettingsStore::~NvsSettingsStore() {
    if (ready_) preferences_.end();
}

bool sameDeviceOrigin(const std::string& origin, const std::string& host,
                      const std::string& device_ip) {
    if (device_ip.empty() || device_ip == "0.0.0.0") return false;
    return (host == device_ip && origin == "http://" + device_ip) ||
           (host == device_ip + ":80" && origin == "http://" + device_ip + ":80");
}

bool parseToolheadId(const std::string& text, int32_t& value) {
    if (text.empty()) return false;
    int32_t parsed = 0;
    for (char ch : text) {
        if (ch < '0' || ch > '9') return false;
        const int digit = ch - '0';
        if (parsed > (INT32_MAX - digit) / 10) return false;
        parsed = parsed * 10 + digit;
    }
    value = parsed;
    return true;
}

std::string renderSettingsPage(const Settings& settings, const std::string& message) {
    const SettingsInput& saved = settings.current();
    std::string html = "<!doctype html><html><head><meta charset=\"utf-8\">"
                       "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
                       "<title>Spool Tap settings</title></head><body><h1>Settings</h1>";
    if (!message.empty()) html += "<p>" + escapeHtml(message) + "</p>";
    if (!settings.error().empty())
        html += "<p role=\"alert\">" + escapeHtml(settings.error()) + "</p>";
    html += "<form method=\"post\" action=\"/settings\">"
            "<label>Spoolman HTTPS base URL <input name=\"spoolman_url\" value=\"" +
            escapeHtml(saved.spoolman_url) + "\" required></label><br>"
            "<label>FilaBridge HTTPS base URL <input name=\"filabridge_url\" value=\"" +
            escapeHtml(saved.filabridge_url) + "\" required></label><br>"
            "<label>Printer name <input name=\"printer_name\" value=\"" +
            escapeHtml(saved.printer_name) + "\" required></label><br>"
            "<label>Toolhead ID <input name=\"toolhead_id\" type=\"number\" min=\"0\" max=\"2147483647\" value=\"" +
            std::to_string(saved.toolhead_id) + "\" required></label><br>"
            "<label>Scan reader ID <input name=\"reader_id\" value=\"" +
            escapeHtml(saved.reader_id) + "\" required></label><br>"
            "<button type=\"submit\">Save settings</button></form>"
            "<p>Service hosts need Let's Encrypt certificates.</p>"
            "<p>Wi-Fi and update credentials are set at build time.</p>"
            "<p><a href=\"/logs\">Recent events</a></p></body></html>";
    return html;
}
