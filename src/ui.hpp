#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "tap_types.hpp"

enum class UiAction { None, Load, Unload, Confirm, Cancel, Back, Status };
enum class UiScreen { Home, Tag, Working, Error, Confirm, Status, Update };

struct UiView {
    UiScreen screen = UiScreen::Working;
    std::string title;
    std::string status;
    std::string activity;
    std::string color_hex;
    UiAction left = UiAction::None;
    UiAction right = UiAction::None;
    bool stale = false;
    bool wifi = false;
    bool nfc = false;
    bool bridge = false;
    ApiError spool_error = ApiError::None;
    ApiError mapping_error = ApiError::None;
};

class Ui {
public:
    void setConnections(bool wifi, bool nfc, bool bridge);
    void showHome(const Mapping& mapping, const SpoolInfo* spool, ApiError spool_error = ApiError::None,
                  ApiError mapping_error = ApiError::None);
    void showTag(const TagRead& tag, const TagHints& hints, const ScanMatch& match,
                 const SpoolInfo* current_spool);
    void showWorking(const char* activity);
    void setHomeActivity(const char* activity);
    bool homeMatches(const Mapping& mapping, const SpoolInfo* spool,
                     ApiError spool_error = ApiError::None) const;
    void showError(ApiError error, const char* failed_operation = nullptr);
    void showConfirm(UiAction operation, const SpoolInfo& spool);
    void showStatus(const std::string& ip, const std::string& version);
    void showUpdateProgress(size_t bytes);
    void showUpdateResult(bool success);
    std::vector<std::string> visibleLines(size_t slots) const;
    UiAction actionAt(int x, int y) const;
    UiAction pollAction() const;
    void render() const;
    void renderHomeActivity() const;
    const UiView& view() const { return view_; }

private:
    struct Line { std::string text; uint8_t priority; };
    void start(UiScreen screen, const std::string& title);
    std::string connectionText(bool bridge) const;
    UiView view_;
    std::vector<Line> lines_;
    bool wifi_ = false;
    bool nfc_ = false;
    bool bridge_ = false;
};
