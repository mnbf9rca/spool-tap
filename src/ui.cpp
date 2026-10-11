#include "ui.hpp"

#include <cmath>
#include <cstdint>

#ifndef SPOOL_TAP_UNIT_TEST
#include <M5Unified.h>
#endif

namespace {
constexpr size_t kCharsPerLine = 17;
constexpr int kButtonsTop = 184;

std::string safeText(const std::string& input) {
    std::string result;
    for (size_t i = 0; i < input.size();) {
        const uint8_t first = static_cast<uint8_t>(input[i]);
        if (first < 0x80) {
            result += first >= 0x20 && first != 0x7F ? static_cast<char>(first) : ' ';
            ++i;
            continue;
        }
        const size_t length = first >= 0xC2 && first <= 0xDF ? 2 :
                              first >= 0xE0 && first <= 0xEF ? 3 :
                              first >= 0xF0 && first <= 0xF4 ? 4 : 0;
        uint32_t point = length == 2 ? first & 0x1F : length == 3 ? first & 0x0F : first & 0x07;
        bool valid = length && i + length <= input.size();
        for (size_t j = 1; valid && j < length; ++j) {
            const uint8_t byte = static_cast<uint8_t>(input[i + j]);
            valid = (byte & 0xC0) == 0x80;
            point = (point << 6) | (byte & 0x3F);
        }
        valid = valid && point >= (length == 2 ? 0x80u : length == 3 ? 0x800u : 0x10000u) &&
                point <= 0x10FFFF && !(point >= 0xD800 && point <= 0xDFFF) &&
                !(point >= 0x80 && point <= 0x9F);
        if (valid) result.append(input, i, length);
        else result += '?';
        i += valid ? length : 1;
    }
    return result;
}

std::vector<std::string> wrap(const std::string& text) {
    std::vector<std::string> result;
    std::string rest = safeText(text);
    while (!rest.empty() && result.size() < 2) {
        if (rest.size() <= kCharsPerLine) { result.push_back(rest); rest.clear(); break; }
        size_t cut = kCharsPerLine;
        while (cut > 0 && (static_cast<uint8_t>(rest[cut]) & 0xC0) == 0x80) --cut;
        const size_t space = rest.rfind(' ', cut);
        if (space != std::string::npos && space > 0) cut = space;
        result.push_back(rest.substr(0, cut));
        rest.erase(0, cut);
        while (!rest.empty() && rest[0] == ' ') rest.erase(0, 1);
    }
    if (!rest.empty() && result.size() == 2) {
        std::string& last = result.back();
        while (last.size() > kCharsPerLine - 3) {
            size_t start = last.size() - 1;
            while (start > 0 && (static_cast<uint8_t>(last[start]) & 0xC0) == 0x80) --start;
            last.erase(start);
        }
        last += "...";
    }
    return result;
}

std::string remainingText(double grams) {
    if (!std::isfinite(grams) || grams < 0) return "Remaining unknown";
    return std::to_string(static_cast<long>(std::lround(grams))) + " g remaining";
}

std::string spoolMaterial(const SpoolInfo& spool) {
    if (spool.vendor.empty()) return spool.material;
    if (spool.material.empty()) return spool.vendor;
    return spool.vendor + " " + spool.material;
}

const char* errorText(ApiError error) {
    switch (error) {
    case ApiError::Wifi: return "Wi-Fi unavailable";
    case ApiError::Time: return "Time not set";
    case ApiError::Dns: return "DNS failed";
    case ApiError::Tls: return "TLS error";
    case ApiError::Timeout: return "Request timed out";
    case ApiError::TooLarge: return "Response too large";
    case ApiError::InvalidUrl: return "Invalid HTTPS URL";
    case ApiError::Malformed: return "Invalid service response";
    case ApiError::MissingSpool: return "Spool not found";
    case ApiError::ArchivedSpool: return "Spool archived";
    case ApiError::Conflict: return "Spool already in use";
    case ApiError::InvalidTag: return "Tag UID unreadable";
    case ApiError::Http: return "HTTP request failed";
    case ApiError::None: return "Ready";
    }
    return "HTTP request failed";
}

#ifndef SPOOL_TAP_UNIT_TEST
const char* actionLabel(UiAction action) {
    switch (action) {
    case UiAction::Load: return "Load";
    case UiAction::Unload: return "Unload";
    case UiAction::Confirm: return "Confirm";
    case UiAction::Cancel: return "Cancel";
    case UiAction::Back: return "Back";
    case UiAction::Status: return "Status";
    case UiAction::None: return "";
    }
    return "";
}

int hexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

void drawButton(UiAction action, int x) {
    if (action == UiAction::None) return;
    M5.Display.fillRect(x + 4, kButtonsTop + 4, 152, 48, TFT_DARKGREEN);
    M5.Display.drawRect(x + 4, kButtonsTop + 4, 152, 48, TFT_WHITE);
    M5.Display.setTextColor(TFT_WHITE, TFT_DARKGREEN);
    M5.Display.setTextSize(3);
    M5.Display.setCursor(x + 12, kButtonsTop + 16);
    M5.Display.print(actionLabel(action));
}
#endif
} // namespace

void Ui::start(UiScreen screen, const std::string& title) {
    view_ = UiView{};
    view_.screen = screen;
    view_.title = safeText(title);
    view_.wifi = wifi_;
    view_.nfc = nfc_;
    view_.bridge = bridge_;
    lines_.clear();
}

void Ui::setPrinterName(const std::string& name) { printer_ = safeText(name); }
void Ui::setConnections(bool wifi, bool nfc, bool bridge) {
    wifi_ = wifi;
    nfc_ = nfc;
    bridge_ = bridge;
    view_.wifi = wifi;
    view_.nfc = nfc;
    view_.bridge = bridge && !view_.stale;
}

std::string Ui::connectionText(bool bridge) const {
    return std::string("Wi-Fi") + (wifi_ ? "+" : "-") + " NFC" + (nfc_ ? "+" : "-") +
           " Bridge" + (bridge ? "+" : "-");
}

void Ui::showHome(const Mapping& mapping, const SpoolInfo* spool) {
    start(UiScreen::Home, printer_);
    view_.status = connectionText(bridge_ && !mapping.stale);
    view_.stale = mapping.stale;
    if (mapping.stale) view_.bridge = false;
    view_.right = UiAction::Status;
    if (mapping.stale) lines_.push_back({"Stale", 0});
    if (!mapping.spool_id) {
        lines_.push_back({"No spool loaded", 0});
        lines_.push_back({"Tap reel to load", 2});
        return;
    }
    if (!spool || spool->id != mapping.spool_id) {
        lines_.push_back({"Spool details unavailable", 0});
        return;
    }
    lines_.push_back({"LOADED SPOOL", 3});
    const std::string material = spoolMaterial(*spool);
    if (!material.empty()) lines_.push_back({safeText(material), 0});
    if (!spool->name.empty()) lines_.push_back({safeText(spool->name), 1});
    lines_.push_back({remainingText(spool->remaining_g), 0});
    lines_.push_back({"Tap reel to load another", 2});
    view_.color_hex = spool->color_hex;
    if (!mapping.stale) view_.left = UiAction::Unload;
}

void Ui::showTag(const TagRead& tag, const TagHints& hints, const ScanMatch& match,
                 const SpoolInfo* current_spool) {
    if (match.kind == ScanKind::Spool && match.spool.archived) {
        showError(ApiError::ArchivedSpool);
    } else if (match.kind == ScanKind::Spool && match.spool.id == match.spool_id && match.spool_id) {
        start(UiScreen::Tag, "Spool linked");
        const std::string material = spoolMaterial(match.spool);
        if (!material.empty()) lines_.push_back({safeText(material), static_cast<uint8_t>(tag.ndef_malformed ? 2 : 0)});
        if (!match.spool.name.empty()) lines_.push_back({safeText(match.spool.name), 0});
        lines_.push_back({remainingText(match.spool.remaining_g), 0});
        if (tag.ndef_malformed) lines_.push_back({"Tag data unreadable", 1});
        if (current_spool && current_spool->id) {
            lines_.push_back({"Current spool", 2});
            lines_.push_back({safeText(current_spool->name), static_cast<uint8_t>(tag.ndef_malformed ? 2 : 1)});
        }
        view_.color_hex = match.spool.color_hex;
        view_.left = UiAction::Load;
        view_.right = UiAction::Cancel;
        return;
    } else if (match.kind == ScanKind::FilamentOnly) {
        start(UiScreen::Tag, "Filament found");
        lines_.push_back({"No spool linked", 0});
        lines_.push_back({"Assign in Spoolman", 0});
        if (tag.ndef_malformed) lines_.push_back({"Tag data unreadable", 1});
        lines_.push_back({safeText(tag.uid), 2});
    } else {
        start(UiScreen::Tag, "No spool linked");
        lines_.push_back({safeText(tag.uid), 0});
        if (tag.ndef_malformed) lines_.push_back({"Tag data unreadable", 1});
        else {
            lines_.push_back({safeText(tag.type), 1});
            const std::string material = hints.material_type.empty() ? hints.material : hints.material_type;
            if (!hints.brand.empty() || !material.empty())
                lines_.push_back({safeText(hints.brand + " " + material), 1});
            if (!hints.material_type.empty() && !hints.material.empty())
                lines_.push_back({safeText(hints.material), 2});
            if (!hints.identifier.empty()) lines_.push_back({safeText(hints.identifier), 3});
            if (std::isfinite(hints.nominal_weight_g) && hints.nominal_weight_g > 0)
                lines_.push_back({"Nominal " + std::to_string(static_cast<long>(std::lround(hints.nominal_weight_g))) + "g", 3});
            if (!hints.min_temp_c.empty() || !hints.max_temp_c.empty())
                lines_.push_back({safeText(hints.min_temp_c + "-" + hints.max_temp_c + "C"), 3});
            view_.color_hex = hints.color_hex;
        }
        lines_.push_back({"Assign in Spoolman", 0});
    }
    view_.right = UiAction::Back;
}

void Ui::showWorking() {
    start(UiScreen::Working, "Working");
    view_.status = connectionText(bridge_);
    lines_.push_back({"Please wait", 0});
}

void Ui::showError(ApiError error) {
    start(UiScreen::Error, "Error");
    view_.status = connectionText(bridge_);
    lines_.push_back({errorText(error), 0});
    view_.right = UiAction::Back;
}

void Ui::showConfirm(UiAction operation, const SpoolInfo& spool) {
    if (operation != UiAction::Load && operation != UiAction::Unload) return;
    start(UiScreen::Confirm, operation == UiAction::Load ? "Load spool?" : "Unload spool?");
    lines_.push_back({safeText(spool.name), 0});
    const std::string material = spoolMaterial(spool);
    if (!material.empty()) lines_.push_back({safeText(material), 0});
    view_.left = UiAction::Confirm;
    view_.right = UiAction::Cancel;
}

void Ui::showSuccess(bool loading, const SpoolInfo& spool) {
    start(UiScreen::Success, loading ? "Spool loaded" : "Spool unloaded");
    if (!spool.name.empty()) lines_.push_back({safeText(spool.name), 0});
    lines_.push_back({"Mapping confirmed", 1});
    view_.right = UiAction::Back;
}

void Ui::showStatus(const std::string& ip, const std::string& version) {
    start(UiScreen::Status, "Status");
    lines_.push_back({"IP " + safeText(ip), 0});
    lines_.push_back({"Version " + safeText(version), 0});
    lines_.push_back({"/settings", 0});
    lines_.push_back({"/logs", 0});
    lines_.push_back({"/update", 0});
    view_.right = UiAction::Back;
}

void Ui::showUpdateProgress(size_t bytes) {
    start(UiScreen::Update, "Updating");
    lines_.push_back({std::to_string(bytes / 1024) + " KB received", 0});
    lines_.push_back({"Keep power on", 0});
}

void Ui::showUpdateResult(bool success) {
    start(UiScreen::Update, success ? "Update ready" : "Update failed");
    lines_.push_back({success ? "Rebooting" : "Current image kept", 0});
    if (!success) view_.right = UiAction::Back;
}

std::vector<std::string> Ui::visibleLines(size_t slots) const {
    if (!slots) return {};
    std::vector<std::string> result;
    for (int priority = 3; priority >= 0; --priority) {
        result.clear();
        for (const Line& line : lines_) {
            if (line.priority > priority) continue;
            const auto parts = wrap(line.text);
            result.insert(result.end(), parts.begin(), parts.end());
        }
        if (result.size() <= slots) return result;
    }
    while (result.size() > slots && result.size() > 1) result.erase(result.end() - 2);
    return result;
}

UiAction Ui::actionAt(int x, int y) const {
    if (x < 0 || x >= 320 || y < kButtonsTop || y >= 240) return UiAction::None;
    return x < 160 ? view_.left : view_.right;
}

#ifndef SPOOL_TAP_UNIT_TEST
UiAction Ui::pollAction() const {
    if (!M5.Touch.getCount()) return UiAction::None;
    const auto& touch = M5.Touch.getDetail();
    return touch.wasPressed() ? actionAt(touch.x, touch.y) : UiAction::None;
}

void Ui::render() const {
    const uint32_t background = view_.screen == UiScreen::Error ? TFT_MAROON :
                                view_.screen == UiScreen::Working ? TFT_NAVY :
                                view_.screen == UiScreen::Confirm ? TFT_DARKGREEN : TFT_BLACK;
    M5.Display.fillScreen(background);
    M5.Display.setTextColor(TFT_WHITE, background);
    M5.Display.setTextSize(3);
    int y = 4;
    for (const auto& line : wrap(view_.title)) {
        M5.Display.setCursor(4, y);
        M5.Display.print(line.c_str());
        y += 26;
    }
    if (!view_.status.empty()) {
        M5.Display.setTextSize(3);
        M5.Display.setCursor(4, y);
        M5.Display.print(view_.wifi ? "Wi-Fi+" : "Wi-Fi-");
        M5.Display.setCursor(112, y);
        M5.Display.print(view_.nfc ? "NFC+" : "NFC-");
        M5.Display.setCursor(190, y);
        M5.Display.print(view_.bridge ? "Bridge+" : "Bridge-");
        y += 28;
    }
    if (view_.color_hex.size() == 6 && view_.title.size() <= 15) {
        uint32_t rgb = 0;
        bool valid = true;
        for (char c : view_.color_hex) {
            const int nibble = hexNibble(c);
            if (nibble < 0) { valid = false; break; }
            rgb = (rgb << 4) | static_cast<uint32_t>(nibble);
        }
        if (valid) {
            M5.Display.fillRect(293, 6, 20, 20, M5.Display.color565(rgb >> 16, rgb >> 8, rgb));
            M5.Display.drawRect(293, 6, 20, 20, TFT_WHITE);
        }
    }
    M5.Display.setTextSize(3);
    const size_t slots = y < kButtonsTop ? static_cast<size_t>((kButtonsTop - y) / 27) : 0;
    for (const auto& line : visibleLines(slots)) {
        M5.Display.setCursor(4, y);
        M5.Display.print(line.c_str());
        y += 27;
    }
    drawButton(view_.left, 0);
    drawButton(view_.right, 160);
}
#else
UiAction Ui::pollAction() const { return UiAction::None; }
void Ui::render() const {}
#endif
