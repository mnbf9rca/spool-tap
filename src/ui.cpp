#include "ui.hpp"

#include <cmath>
#include <cstdint>

#ifndef SPOOL_TAP_UNIT_TEST
#include <M5Unified.h>
#endif

namespace {
constexpr size_t kCharsPerLine = 18;
constexpr int kButtonsTop = 184;
constexpr int kHomeActivityTop = 162;

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
    case ApiError::MissingPrinter: return "Printer not found";
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
    M5.Display.setFont(&fonts::DejaVu24);
    M5.Display.setTextSize(1);
    M5.Display.drawString(actionLabel(action), x + 12, kButtonsTop + 16);
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

void Ui::setConnections(bool wifi, bool nfc, bool bridge) {
    wifi_ = wifi;
    nfc_ = nfc;
    bridge_ = bridge;
    view_.wifi = wifi;
    view_.nfc = nfc;
    view_.bridge = bridge && !view_.stale;
    if (view_.screen == UiScreen::Home || view_.screen == UiScreen::Working ||
        view_.screen == UiScreen::Error) view_.status = connectionText(view_.bridge);
}

std::string Ui::connectionText(bool bridge) const {
    if (!wifi_) return "Wi-Fi disconnected";
    if (!nfc_) return "NFC reader unavailable";
    if (view_.spool_error == ApiError::MissingSpool) return "Spool not found";
    if (view_.spool_error == ApiError::ArchivedSpool) return "Spool archived";
    if (view_.spool_error != ApiError::None) return "Spoolman unavailable";
    if (view_.mapping_error == ApiError::MissingPrinter) return "Printer not found";
    if (!bridge) return "FilaBridge unavailable";
    return "";
}

void Ui::showHome(const Mapping& mapping, const SpoolInfo* spool, ApiError spool_error,
                  ApiError mapping_error) {
    start(UiScreen::Home, "");
    view_.spool_error = spool_error;
    view_.mapping_error = mapping_error;
    view_.status = connectionText(bridge_ && !mapping.stale);
    view_.stale = mapping.stale;
    if (mapping.stale) view_.bridge = false;
    view_.right = UiAction::Status;
    if (mapping.stale) {
        if (spool && spool->id == mapping.spool_id) {
            lines_.push_back({"Last known spool:", 0});
            const std::string material = spoolMaterial(*spool);
            if (!material.empty()) lines_.push_back({safeText(material), 0});
            if (!spool->name.empty()) lines_.push_back({safeText(spool->name), 0});
            lines_.push_back({remainingText(spool->remaining_g), 0});
        } else lines_.push_back({"Mapping unavailable", 0});
        return;
    }
    if (!mapping.spool_id) {
        lines_.push_back({"No spool loaded", 0});
        lines_.push_back({" ", 2});
        lines_.push_back({"Tap reel to load", 2});
        return;
    }
    if (!spool || spool->id != mapping.spool_id) {
        lines_.push_back({"Spool details unavailable", 0});
        return;
    }
    const std::string material = spoolMaterial(*spool);
    if (!material.empty()) lines_.push_back({safeText(material), 0});
    if (!spool->name.empty()) lines_.push_back({safeText(spool->name), 1});
    lines_.push_back({remainingText(spool->remaining_g), 0});
    lines_.push_back({" ", 2});
    lines_.push_back({"Tap reel to load", 2});
    if (!mapping.stale) view_.left = UiAction::Unload;
}

void Ui::setHomeActivity(const char* activity) {
    if (view_.screen == UiScreen::Home) view_.activity = activity;
}

bool Ui::homeMatches(const Mapping& mapping, const SpoolInfo* spool, ApiError spool_error) const {
    if (view_.screen != UiScreen::Home || !view_.status.empty()) return false;
    Ui next = *this;
    next.showHome(mapping, spool, spool_error);
    return next.view_.status.empty() && next.view_.left == view_.left &&
           next.view_.right == view_.right && next.visibleLines(5) == visibleLines(5);
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
            lines_.push_back({current_spool->name.empty() ? "Current spool" :
                              safeText("Current: " + current_spool->name), 2});
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
            const std::string material = hints.material_type.empty() ? hints.material : hints.material_type;
            const bool has_details = !hints.brand.empty() || !material.empty() ||
                                     !hints.identifier.empty() ||
                                     (std::isfinite(hints.nominal_weight_g) && hints.nominal_weight_g > 0);
            if (!has_details) lines_.push_back({safeText(tag.type), 1});
            if (!hints.brand.empty() || !material.empty())
                lines_.push_back({safeText(hints.brand + " " + material), 0});
            if (!hints.material_type.empty() && !hints.material.empty())
                lines_.push_back({safeText(hints.material), 3});
            if (!hints.identifier.empty()) lines_.push_back({safeText(hints.identifier), 3});
            if (std::isfinite(hints.nominal_weight_g) && hints.nominal_weight_g > 0)
                lines_.push_back({"Nominal " + std::to_string(static_cast<long>(std::lround(hints.nominal_weight_g))) + " g", 1});
            if (!hints.min_temp_c.empty() || !hints.max_temp_c.empty())
                lines_.push_back({safeText(hints.min_temp_c + "-" + hints.max_temp_c + "C"), 3});
            view_.color_hex = hints.color_hex;
        }
        lines_.push_back({"Assign in Spoolman", 0});
    }
    view_.right = UiAction::Back;
}

void Ui::showWorking(const char* activity) {
    start(UiScreen::Working, activity);
    view_.status = connectionText(bridge_);
    lines_.push_back({"Please wait", 0});
}

void Ui::showError(ApiError error, const char* failed_operation) {
    if (error == ApiError::Wifi) {
        start(UiScreen::Error, "");
        view_.status = connectionText(false);
        lines_.push_back({"Check Wi-Fi", 0});
    } else if (error == ApiError::MissingPrinter) {
        start(UiScreen::Error, "");
        view_.mapping_error = error;
        view_.status = connectionText(false);
        lines_.push_back({"Check printer name", 0});
        lines_.push_back({"in Settings", 0});
        lines_.push_back({"matches FilaBridge", 0});
    } else if (error == ApiError::ArchivedSpool || error == ApiError::MissingSpool) {
        start(UiScreen::Error, errorText(error));
        lines_.push_back({error == ApiError::ArchivedSpool ? "Unarchive in Spoolman" :
                          "Check in Spoolman", 0});
    } else if (failed_operation && error != ApiError::InvalidTag) {
        start(UiScreen::Error, "");
        view_.spool_error = error;
        view_.status = connectionText(bridge_);
        lines_.push_back({failed_operation, 0});
        lines_.push_back({errorText(error), 0});
        lines_.push_back({"Back to retry", 0});
    } else {
        start(UiScreen::Error, "Error");
        view_.status = connectionText(bridge_);
        lines_.push_back({errorText(error), 0});
        if (error == ApiError::InvalidTag) lines_.push_back({"Tap tag again", 0});
    }
    view_.right = UiAction::Back;
}

void Ui::showConfirm(UiAction operation, const SpoolInfo& spool) {
    if (operation != UiAction::Load && operation != UiAction::Unload) return;
    start(UiScreen::Confirm, operation == UiAction::Load ? "Load spool?" : "Unload spool?");
    const std::string material = spoolMaterial(spool);
    if (!material.empty()) lines_.push_back({safeText(material), 0});
    if (!spool.name.empty()) lines_.push_back({safeText(spool.name), 0});
    view_.left = UiAction::Confirm;
    view_.right = UiAction::Cancel;
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
    const uint32_t background = view_.screen == UiScreen::Working ? TFT_NAVY :
                                view_.screen == UiScreen::Confirm ? TFT_DARKGREEN : TFT_BLACK;
    M5.Display.fillScreen(background);
    M5.Display.setTextColor(TFT_WHITE, background);
    M5.Display.setFont(&fonts::DejaVu24);
    M5.Display.setTextSize(1);
    M5.Display.setTextDatum(textdatum_t::top_left);
    int y = 4;
    for (const auto& line : wrap(view_.title)) {
        M5.Display.drawString(line.c_str(), 4, y);
        y += 26;
    }
    if (!view_.status.empty()) {
        M5.Display.setTextColor(TFT_RED, background);
        M5.Display.drawString(view_.status.c_str(), 4, y);
        M5.Display.setTextColor(TFT_WHITE, background);
        y += 28;
    }
    if (view_.color_hex.size() == 6) {
        uint32_t rgb = 0;
        bool valid = true;
        for (char c : view_.color_hex) {
            const int nibble = hexNibble(c);
            if (nibble < 0) { valid = false; break; }
            rgb = (rgb << 4) | static_cast<uint32_t>(nibble);
        }
        if (valid) M5.Display.fillRect(312, 0, 8, kHomeActivityTop,
                                        M5.Display.color565(rgb >> 16, rgb >> 8, rgb));
    }
    const int contentBottom = view_.screen == UiScreen::Home ? kHomeActivityTop : kButtonsTop;
    const size_t slots = y < contentBottom ? static_cast<size_t>((contentBottom - y) / 27) : 0;
    for (const auto& line : visibleLines(slots)) {
        M5.Display.drawString(line.c_str(), 4, y);
        y += 27;
    }
    if (view_.screen == UiScreen::Home) renderHomeActivity();
    drawButton(view_.left, 0);
    drawButton(view_.right, 160);
}

void Ui::renderHomeActivity() const {
    if (view_.screen != UiScreen::Home) return;
    M5.Display.fillRect(0, kHomeActivityTop, 320, kButtonsTop - kHomeActivityTop, TFT_BLACK);
    if (view_.activity.empty()) return;
    M5.Display.setFont(&fonts::DejaVu12);
    M5.Display.setTextSize(1);
    M5.Display.setTextDatum(textdatum_t::top_left);
    M5.Display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    M5.Display.drawString(view_.activity.c_str(),
                          316 - M5.Display.textWidth(view_.activity.c_str()), kHomeActivityTop);
}
#else
UiAction Ui::pollAction() const { return UiAction::None; }
void Ui::render() const {}
void Ui::renderHomeActivity() const {}
#endif
