#include "app.hpp"

bool App::showMapping(const Mapping& observed, uint32_t now) {
    Ui& ui = ui_.ui();
    SpoolInfo details;
    if (observed.spool_id) {
        ApiError error = ApiError::None;
        if (!services_.getSpool(observed.spool_id, details, error)) {
            const bool same_spool = current_spool_.id == observed.spool_id;
            mapping_ = observed;
            mapping_.stale = true;
            if (!same_spool) current_spool_ = {};
            have_home_ = true;
            refresh_pending_ = true;
            if (same_spool) ui.showHome(mapping_, &current_spool_);
            else ui.showError(error);
            ui.render();
            return false;
        }
    }
    mapping_ = observed;
    mapping_.stale = false;
    current_spool_ = details;
    have_home_ = true;
    refresh_pending_ = false;
    last_refresh_ = now;
    ui.showHome(mapping_, mapping_.spool_id ? &current_spool_ : nullptr);
    ui.render();
    return true;
}

bool App::refreshHome(uint32_t now) {
    Ui& ui = ui_.ui();
    ui.showWorking();
    ui.render();
    Mapping observed;
    ApiError error = ApiError::None;
    if (!services_.getMapping(observed, error)) {
        bridge_ready_ = false;
        refresh_pending_ = true;
        if (have_home_) {
            mapping_.stale = true;
            ui.showHome(mapping_, current_spool_.id == mapping_.spool_id ? &current_spool_ : nullptr);
        } else ui.showError(error);
        ui.render();
        return false;
    }
    bridge_ready_ = true;
    return showMapping(observed, now);
}

void App::confirm(UiAction operation, uint32_t now) {
    Ui& ui = ui_.ui();
    pending_ = UiAction::None;
    ui.showWorking();
    ui.render();
    Mapping fresh;
    ApiError error = ApiError::None;
    if (!services_.getMapping(fresh, error)) {
        bridge_ready_ = false;
        mapping_.stale = true;
        refresh_pending_ = true;
        ui.showError(error);
        ui.render();
        return;
    }
    bridge_ready_ = true;
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
    const SpoolInfo previous_spool = current_spool_;
    Mapping after = mapping_;
    const bool mapped = services_.mapToolhead(target, after, error);
    if (!mapped) {
        refreshHome(now);
        ui.showError(error);
        ui.render();
        return;
    }
    if (!showMapping(after, now)) return;
    ui.showSuccess(operation == UiAction::Load,
                   operation == UiAction::Load ? current_spool_ : previous_spool);
    ui.render();
}

void App::settingsChanged() {
    candidate_ = {};
    candidate_tag_ = {};
    candidate_hints_ = {};
    pending_ = UiAction::None;
    mapping_.stale = true;
    refresh_pending_ = true;
    Ui& ui = ui_.ui();
    ui.showHome(mapping_, current_spool_.id == mapping_.spool_id ? &current_spool_ : nullptr);
    ui.render();
}

void App::tick() {
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
        ui.showStatus(ui_.ipAddress(), "v1");
        ui.render();
    } else if (action == UiAction::Back || action == UiAction::Cancel) {
        pending_ = UiAction::None;
        candidate_ = {};
        if (have_home_) {
            attempted_ = true;
            last_attempt_ = now;
            if (!refreshHome(now)) {
                ui.showHome(mapping_, current_spool_.id == mapping_.spool_id ? &current_spool_ : nullptr);
                ui.render();
            }
        } else {
            ui.showWorking();
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
    if (!presented || before != UiScreen::Home || action != UiAction::None ||
        !have_home_ || mapping_.stale) return;
    ui.showWorking();
    ui.render();
    ScanMatch match;
    ApiError error = ApiError::None;
    if (!services_.scan(tag, match, error)) {
        ui.showError(error);
    } else {
        candidate_tag_ = tag;
        candidate_hints_ = hints;
        candidate_ = match;
        ui.showTag(tag, hints, match, current_spool_.id == mapping_.spool_id ? &current_spool_ : nullptr);
    }
    ui.render();
}
