#include "app.hpp"

#include "logs.hpp"
#include "ota.hpp"

#ifndef SPOOL_TAP_VERSION
#define SPOOL_TAP_VERSION "v1.0.0"
#endif

namespace {
const char* errorName(ApiError error) {
    switch (error) {
    case ApiError::None: return "none";
    case ApiError::Wifi: return "Wi-Fi unavailable";
    case ApiError::Time: return "time not set";
    case ApiError::Dns: return "DNS failed";
    case ApiError::Tls: return "TLS failed";
    case ApiError::Timeout: return "timeout";
    case ApiError::Http: return "HTTP failed";
    case ApiError::TooLarge: return "response too large";
    case ApiError::InvalidUrl: return "invalid URL";
    case ApiError::Malformed: return "invalid response";
    case ApiError::MissingPrinter: return "configured printer missing";
    case ApiError::MissingSpool: return "spool missing";
    case ApiError::ArchivedSpool: return "spool archived";
    case ApiError::Conflict: return "conflict";
    case ApiError::InvalidTag: return "invalid tag";
    }
    return "unknown error";
}

void addField(std::string& text, const char* key, const std::string& value) {
    if (!value.empty()) text += std::string(" ") + key + "=" + value;
}

std::string spoolDetail(const SpoolInfo& spool) {
    std::string text = "Spoolman spool #" + std::to_string(spool.id);
    addField(text, "vendor", spool.vendor);
    addField(text, "name", spool.name);
    addField(text, "material", spool.material);
    if (spool.remaining_g >= 0) text += " remaining=" + std::to_string(static_cast<int>(spool.remaining_g)) + "g";
    return text;
}

std::string tagDetail(const TagRead& tag, const TagHints& hints) {
    std::string text;
    addField(text, "type", tag.type);
    addField(text, "format", hints.format);
    addField(text, "brand", hints.brand);
    addField(text, "material", hints.material);
    addField(text, "material_type", hints.material_type);
    addField(text, "color", hints.color_hex);
    addField(text, "identifier", hints.identifier);
    if (hints.nominal_weight_g > 0)
        text += " nominal=" + std::to_string(static_cast<int>(hints.nominal_weight_g)) + "g";
    if (tag.ndef_malformed) text += " NDEF unreadable";
    return text;
}

bool sameSpool(const SpoolInfo& a, const SpoolInfo& b) {
    return a.id == b.id && a.vendor == b.vendor && a.name == b.name &&
           a.material == b.material && a.color_hex == b.color_hex &&
           a.remaining_g == b.remaining_g && a.archived == b.archived;
}
}  // namespace

bool App::showMapping(const Mapping& observed, uint32_t now) {
    Ui& ui = ui_.ui();
    SpoolInfo details;
    if (observed.spool_id) {
        if (ui.view().screen == UiScreen::Home) {
            ui.setHomeActivity("Getting spool details...");
            ui.renderHomeActivity();
        } else {
            ui.showWorking("Getting spool details...");
            ui.render();
        }
        ApiError error = ApiError::None;
        if (!services_.getSpool(observed.spool_id, details, error)) {
            if (logs_ && !mapping_.stale)
                logs_->add(LogLevel::Warning, LogEvent::SpoolFailed,
                           "FilaBridge spool #" + std::to_string(observed.spool_id) +
                           "; Spoolman " + errorName(error));
            const bool same_spool = current_spool_.id == observed.spool_id;
            mapping_ = observed;
            mapping_.stale = true;
            if (!same_spool) current_spool_ = {};
            have_home_ = true;
            refresh_pending_ = true;
            if (same_spool) ui.showHome(mapping_, &current_spool_, error);
            else ui.showError(error, "Spool lookup failed");
            ui.render();
            return false;
        }
    }
    const bool same_home = ui.homeMatches(observed, observed.spool_id ? &details : nullptr);
    const bool changed = !have_home_ || mapping_.stale || mapping_.spool_id != observed.spool_id ||
                         !sameSpool(current_spool_, details);
    mapping_ = observed;
    if (logs_ && changed)
        logs_->add(LogLevel::Info, LogEvent::MappingReady,
                   observed.spool_id ? "FilaBridge spool #" + std::to_string(observed.spool_id) +
                                       "; " + spoolDetail(details) : "FilaBridge empty");
    mapping_.stale = false;
    current_spool_ = details;
    have_home_ = true;
    refresh_pending_ = false;
    last_refresh_ = now;
    ui.showHome(mapping_, mapping_.spool_id ? &current_spool_ : nullptr);
    if (same_home) ui.renderHomeActivity();
    else ui.render();
    return true;
}

bool App::refreshHome(uint32_t now) {
    Ui& ui = ui_.ui();
    const bool quiet_retry = !have_home_ && ui.view().screen == UiScreen::Error;
    if (ui.view().screen == UiScreen::Home) {
        ui.setHomeActivity("Checking assignment...");
        ui.renderHomeActivity();
    } else if (!quiet_retry) {
        ui.showWorking("Checking assignment...");
        ui.render();
    }
    Mapping observed;
    ApiError error = ApiError::None;
    if (!services_.getMapping(observed, error)) {
        const bool same_error = quiet_retry && error == last_mapping_error_;
        last_mapping_error_ = error;
        if (logs_ && !mapping_failure_logged_)
            logs_->add(LogLevel::Warning, LogEvent::MappingFailed,
                       std::string("FilaBridge ") + errorName(error));
        mapping_failure_logged_ = true;
        bridge_ready_ = false;
        refresh_pending_ = true;
        if (have_home_) {
            mapping_.stale = true;
            ui.showHome(mapping_, current_spool_.id == mapping_.spool_id ? &current_spool_ : nullptr,
                        ApiError::None, error);
            ui.render();
        } else if (!same_error) {
            ui.showError(error);
            ui.render();
        }
        return false;
    }
    bridge_ready_ = true;
    mapping_failure_logged_ = false;
    last_mapping_error_ = ApiError::None;
    return showMapping(observed, now);
}

void App::confirm(UiAction operation, uint32_t now) {
    Ui& ui = ui_.ui();
    pending_ = UiAction::None;
    ui.showWorking("Checking assignment...");
    ui.render();
    Mapping fresh;
    ApiError error = ApiError::None;
    if (!services_.getMapping(fresh, error)) {
        if (logs_) logs_->add(LogLevel::Warning, LogEvent::MappingFailed,
                              std::string("FilaBridge precheck ") + errorName(error));
        mapping_failure_logged_ = true;
        bridge_ready_ = false;
        mapping_.stale = true;
        refresh_pending_ = true;
        ui.showError(error);
        ui.render();
        return;
    }
    bridge_ready_ = true;
    mapping_failure_logged_ = false;
    last_mapping_error_ = ApiError::None;
    if (fresh.spool_id != mapping_.spool_id) {
        if (showMapping(fresh, now) && operation == UiAction::Load) {
            ui.showTag(candidate_tag_, candidate_hints_, candidate_,
                       current_spool_.id ? &current_spool_ : nullptr);
            ui.render();
        }
        return;
    }

    // shortcut: FilaBridge has no conditional mapping API; another remap can race this precheck.
    const uint32_t target = operation == UiAction::Load ? candidate_.spool_id : 0;
    Mapping after = mapping_;
    ui.showWorking(operation == UiAction::Load ? "Loading spool" : "Unloading spool");
    ui.render();
    const bool mapped = services_.mapToolhead(target, after, error);
    if (!mapped) {
        if (logs_) logs_->add(LogLevel::Error, LogEvent::MappingWriteFailed,
                              "FilaBridge target #" + std::to_string(target) +
                              "; " + errorName(error));
        refreshHome(now);
        ui.showError(error);
        ui.render();
        return;
    }
    if (!showMapping(after, now)) return;
    if (logs_) logs_->add(LogLevel::Info, operation == UiAction::Load ?
                         LogEvent::LoadConfirmed : LogEvent::UnloadConfirmed,
                         "FilaBridge spool #" + std::to_string(after.spool_id));
}

void App::settingsChanged() {
    if (logs_) logs_->add(LogLevel::Info, LogEvent::SettingsChanged);
    candidate_ = {};
    candidate_tag_ = {};
    candidate_hints_ = {};
    pending_ = UiAction::None;
    mapping_.stale = true;
    last_mapping_error_ = ApiError::None;
    refresh_pending_ = true;
    attempted_ = false;
    Ui& ui = ui_.ui();
    ui.showHome(mapping_, current_spool_.id == mapping_.spool_id ? &current_spool_ : nullptr);
    ui.render();
}

void App::tick() {
    if (ota_ && (ota_->busy() || ota_->staged() || ota_->ready())) return;
    if (ota_ && ota_->failed()) {
        if (ui_.pollAction() == UiAction::Back) {
            ota_->dismissFailure();
            refresh_pending_ = true;
            attempted_ = false;
            ui_.ui().showWorking("Checking assignment...");
            ui_.ui().render();
        }
        return;
    }
    if (settings_change_pending_ && *settings_change_pending_) {
        *settings_change_pending_ = false;
        settingsChanged();
    }
    Ui& ui = ui_.ui();
    const uint32_t now = clock_.millis();
    const UiAction action = ui_.pollAction();
    if ((refresh_pending_ || (have_home_ && ui.view().screen == UiScreen::Home &&
                             now - last_refresh_ >= 30000)) &&
        (!attempted_ || now - last_attempt_ >= 2000)) {
        attempted_ = true;
        last_attempt_ = now;
        refreshHome(now);
        return;
    }

    const UiScreen before = ui.view().screen;
    if (action == UiAction::Status && before == UiScreen::Home) {
        ui.showStatus(ui_.ipAddress(), SPOOL_TAP_VERSION);
        ui.render();
    } else if (action == UiAction::Back || action == UiAction::Cancel) {
        pending_ = UiAction::None;
        candidate_ = {};
        if (have_home_) {
            attempted_ = true;
            last_attempt_ = now;
            if (!refreshHome(now)) {
                ui.showHome(mapping_, current_spool_.id == mapping_.spool_id ? &current_spool_ : nullptr,
                            ApiError::None, last_mapping_error_);
                ui.render();
            }
        } else {
            ui.showWorking("Checking assignment...");
            ui.render();
        }
    } else if (action == UiAction::Load && before == UiScreen::Tag &&
               ui.view().left == UiAction::Load && candidate_.kind == ScanKind::Spool &&
               !mapping_.stale) {
        pending_ = UiAction::Load;
        ui.showConfirm(UiAction::Load, candidate_.spool);
        ui.render();
    } else if (action == UiAction::Unload && before == UiScreen::Home &&
               ui.view().left == UiAction::Unload && !mapping_.stale &&
               mapping_.spool_id && current_spool_.id == mapping_.spool_id) {
        pending_ = UiAction::Unload;
        ui.showConfirm(UiAction::Unload, current_spool_);
        ui.render();
    } else if (action == UiAction::Confirm && before == UiScreen::Confirm &&
               pending_ != UiAction::None) {
        confirm(pending_, now);
    }

    TagRead tag;
    TagHints hints;
    const bool presented = reader_.poll(tag, hints);
    const bool scan_screen = before == UiScreen::Home || before == UiScreen::Tag ||
                             before == UiScreen::Error;
    if (!presented || !scan_screen || action != UiAction::None ||
        !have_home_ || mapping_.stale) return;
    ui.showWorking("Checking tag");
    ui.render();
    ScanMatch match;
    ApiError error = ApiError::None;
    if (!services_.scan(tag, match, error)) {
        if (logs_) logs_->add(LogLevel::Warning, LogEvent::ScanFailed,
                              "UID=" + tag.uid + "; Spoolman " + errorName(error) +
                              "; tag" + tagDetail(tag, hints));
        ui.showError(error, "Tag lookup failed");
    } else {
        if (logs_) {
            std::string detail = "UID=" + tag.uid;
            if (match.kind == ScanKind::Spool) detail += "; " + spoolDetail(match.spool);
            else if (match.kind == ScanKind::FilamentOnly)
                detail += "; Spoolman filament #" + std::to_string(match.filament_id) + ", no spool";
            else detail += "; Spoolman no tag match";
            detail += "; tag" + tagDetail(tag, hints);
            logs_->add(LogLevel::Info, LogEvent::TagScanned, detail);
        }
        candidate_tag_ = tag;
        candidate_hints_ = hints;
        candidate_ = match;
        ui.showTag(tag, hints, match, current_spool_.id == mapping_.spool_id ? &current_spool_ : nullptr);
    }
    ui.render();
}
