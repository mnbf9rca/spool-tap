#include <Arduino.h>
#include <unity.h>

#include "app.hpp"
#include "settings.hpp"

namespace {
SpoolInfo spool(uint32_t id) {
    SpoolInfo result;
    result.id = id;
    result.name = "Spool " + std::to_string(id);
    result.material = "PLA";
    result.remaining_g = 120;
    return result;
}

ScanMatch linked(uint32_t id) {
    ScanMatch result;
    result.kind = ScanKind::Spool;
    result.spool_id = id;
    result.spool = spool(id);
    return result;
}

bool hasLine(const Ui& ui, const char* needle) {
    for (const auto& line : ui.visibleLines(5))
        if (line.find(needle) != std::string::npos) return true;
    return false;
}

class FakeReader : public ReaderPort {
public:
    bool present = false;
    bool armed = true;
    TagRead tag;
    TagHints hints;
    int polls = 0;

    FakeReader() { tag.uid = "A1B2"; }
    bool poll(TagRead& out, TagHints& decoded) override {
        ++polls;
        if (!present) { armed = true; return false; }
        if (!armed) return false;
        armed = false;
        out = tag;
        decoded = hints;
        return true;
    }
};

class FakeServices : public ServicesPort {
public:
    uint32_t server_spool = 0;
    bool mapping_ok = true;
    bool spool_ok = true;
    bool scan_ok = true;
    bool map_ok = true;
    ApiError mapping_error = ApiError::Timeout;
    ApiError scan_error = ApiError::Http;
    ApiError map_error = ApiError::Http;
    ScanMatch scan_result = linked(9);
    int mapping_calls = 0;
    int spool_calls = 0;
    int scan_calls = 0;
    int map_calls = 0;
    uint32_t mapped_id = 99;

    bool getMapping(Mapping& out, ApiError& error) override {
        ++mapping_calls;
        if (!mapping_ok) { error = mapping_error; return false; }
        out.spool_id = server_spool;
        out.stale = false;
        error = ApiError::None;
        return true;
    }
    bool getSpool(uint32_t id, SpoolInfo& out, ApiError& error) override {
        ++spool_calls;
        if (!spool_ok) { error = ApiError::MissingSpool; return false; }
        out = spool(id);
        error = ApiError::None;
        return true;
    }
    bool scan(const TagRead&, ScanMatch& out, ApiError& error) override {
        ++scan_calls;
        if (!scan_ok) { error = scan_error; return false; }
        out = scan_result;
        error = ApiError::None;
        return true;
    }
    bool mapToolhead(uint32_t id, Mapping& after, ApiError& error) override {
        ++map_calls;
        mapped_id = id;
        if (map_ok) server_spool = id;
        after.spool_id = server_spool;
        after.stale = false;
        error = map_ok ? ApiError::None : map_error;
        return map_ok;
    }
};

class FakeUi : public UiPort {
public:
    Ui display;
    UiAction next = UiAction::None;
    UiAction pollAction() override {
        const UiAction action = next;
        next = UiAction::None;
        return action;
    }
    Ui& ui() override { return display; }
    std::string ipAddress() const override { return "192.168.1.5"; }
};

class FakeClock : public ClockPort {
public:
    uint32_t now = 0;
    uint32_t millis() const override { return now; }
};

void present(FakeReader& reader, App& app) {
    reader.present = true;
    app.tick();
}

void press(FakeUi& ui, App& app, UiAction action) {
    ui.next = action;
    app.tick();
}

void test_scan_requires_load_and_confirmation_before_mapping() {
    FakeReader reader;
    FakeServices services;
    services.scan_result.spool.name = "Old scan name";
    FakeUi ui;
    FakeClock clock;
    App app(reader, services, ui, clock);
    app.tick();
    present(reader, app);
    TEST_ASSERT_EQUAL_INT(1, services.scan_calls);
    TEST_ASSERT_EQUAL_INT(0, services.map_calls);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(UiScreen::Tag), static_cast<int>(ui.display.view().screen));
    press(ui, app, UiAction::Load);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(UiScreen::Confirm), static_cast<int>(ui.display.view().screen));
    TEST_ASSERT_EQUAL_INT(0, services.map_calls);
    press(ui, app, UiAction::Confirm);
    TEST_ASSERT_EQUAL_INT(1, services.map_calls);
    TEST_ASSERT_EQUAL_UINT32(9, services.mapped_id);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(UiScreen::Success), static_cast<int>(ui.display.view().screen));
    TEST_ASSERT_TRUE(hasLine(ui.display, "Spool 9"));
    TEST_ASSERT_FALSE(hasLine(ui.display, "Old scan name"));
}

void test_unload_names_current_spool_and_maps_zero_only_after_confirmation() {
    FakeReader reader;
    FakeServices services;
    services.server_spool = 7;
    FakeUi ui;
    FakeClock clock;
    App app(reader, services, ui, clock);
    app.tick();
    press(ui, app, UiAction::Unload);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(UiScreen::Confirm), static_cast<int>(ui.display.view().screen));
    TEST_ASSERT_TRUE(hasLine(ui.display, "Spool 7"));
    TEST_ASSERT_EQUAL_INT(0, services.map_calls);
    press(ui, app, UiAction::Confirm);
    TEST_ASSERT_EQUAL_INT(1, services.map_calls);
    TEST_ASSERT_EQUAL_UINT32(0, services.mapped_id);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(UiScreen::Success), static_cast<int>(ui.display.view().screen));
    press(ui, app, UiAction::Back);
    TEST_ASSERT_TRUE(hasLine(ui.display, "No spool loaded"));
}

void test_cancel_never_posts_for_load_or_unload() {
    FakeReader reader;
    FakeServices services;
    services.server_spool = 7;
    FakeUi ui;
    FakeClock clock;
    App app(reader, services, ui, clock);
    app.tick();
    press(ui, app, UiAction::Unload);
    press(ui, app, UiAction::Cancel);
    TEST_ASSERT_EQUAL_INT(0, services.map_calls);
    present(reader, app);
    press(ui, app, UiAction::Load);
    press(ui, app, UiAction::Cancel);
    TEST_ASSERT_EQUAL_INT(0, services.map_calls);
}

void test_changed_mapping_requires_new_load_confirmation() {
    FakeReader reader;
    FakeServices services;
    services.server_spool = 7;
    FakeUi ui;
    FakeClock clock;
    App app(reader, services, ui, clock);
    app.tick();
    present(reader, app);
    press(ui, app, UiAction::Load);
    services.server_spool = 8;
    press(ui, app, UiAction::Confirm);
    TEST_ASSERT_EQUAL_INT(0, services.map_calls);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(UiScreen::Tag), static_cast<int>(ui.display.view().screen));
    TEST_ASSERT_TRUE(hasLine(ui.display, "Spool 8"));
    press(ui, app, UiAction::Load);
    press(ui, app, UiAction::Confirm);
    TEST_ASSERT_EQUAL_INT(1, services.map_calls);
}

void test_idle_refresh_runs_at_thirty_seconds_only_on_home() {
    FakeReader reader;
    FakeServices services;
    services.server_spool = 7;
    FakeUi ui;
    FakeClock clock;
    App app(reader, services, ui, clock);
    app.tick();
    TEST_ASSERT_EQUAL_INT(1, services.mapping_calls);
    clock.now = 29999;
    app.tick();
    TEST_ASSERT_EQUAL_INT(1, services.mapping_calls);
    clock.now = 30000;
    app.tick();
    TEST_ASSERT_EQUAL_INT(2, services.mapping_calls);
    present(reader, app);
    clock.now = 60000;
    app.tick();
    TEST_ASSERT_EQUAL_INT(2, services.mapping_calls);
}

void test_back_requires_tag_removal_before_rescan() {
    FakeReader reader;
    FakeServices services;
    FakeUi ui;
    FakeClock clock;
    App app(reader, services, ui, clock);
    app.tick();
    present(reader, app);
    press(ui, app, UiAction::Back);
    TEST_ASSERT_EQUAL_INT(1, services.scan_calls);
    app.tick();
    TEST_ASSERT_EQUAL_INT(1, services.scan_calls);
    reader.present = false;
    app.tick();
    present(reader, app);
    TEST_ASSERT_EQUAL_INT(2, services.scan_calls);
}

void test_return_to_home_refreshes_mapping_immediately() {
    FakeReader reader;
    FakeServices services;
    FakeUi ui;
    FakeClock clock;
    App app(reader, services, ui, clock);
    app.tick();
    present(reader, app);
    services.server_spool = 7;
    press(ui, app, UiAction::Back);
    TEST_ASSERT_EQUAL_INT(2, services.mapping_calls);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(UiScreen::Home), static_cast<int>(ui.display.view().screen));
    TEST_ASSERT_TRUE(hasLine(ui.display, "Spool 7"));
}

void test_working_screen_ignores_new_scan() {
    FakeReader reader;
    FakeServices services;
    FakeUi ui;
    FakeClock clock;
    App app(reader, services, ui, clock);
    app.tick();
    ui.display.showWorking();
    present(reader, app);
    TEST_ASSERT_EQUAL_INT(0, services.scan_calls);
    TEST_ASSERT_EQUAL_INT(0, services.map_calls);
}

void test_failed_refresh_marks_home_stale_and_hides_unload() {
    FakeReader reader;
    FakeServices services;
    services.server_spool = 7;
    FakeUi ui;
    FakeClock clock;
    App app(reader, services, ui, clock);
    app.tick();
    services.mapping_ok = false;
    clock.now = 30000;
    app.tick();
    TEST_ASSERT_TRUE(ui.display.view().stale);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(UiAction::None), static_cast<int>(ui.display.view().left));
    press(ui, app, UiAction::Unload);
    TEST_ASSERT_EQUAL_INT(0, services.map_calls);
}

void test_failed_spool_refresh_keeps_last_confirmed_details_stale() {
    FakeReader reader;
    FakeServices services;
    services.server_spool = 7;
    FakeUi ui;
    FakeClock clock;
    App app(reader, services, ui, clock);
    app.tick();
    services.spool_ok = false;
    clock.now = 30000;
    app.tick();
    TEST_ASSERT_TRUE(ui.display.view().stale);
    TEST_ASSERT_TRUE(hasLine(ui.display, "Spool 7"));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(UiAction::None), static_cast<int>(ui.display.view().left));
}

void test_missing_current_spool_shows_specific_error_and_disables_unload() {
    FakeReader reader;
    FakeServices services;
    services.server_spool = 7;
    services.spool_ok = false;
    FakeUi ui;
    FakeClock clock;
    App app(reader, services, ui, clock);
    app.tick();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(UiScreen::Error), static_cast<int>(ui.display.view().screen));
    TEST_ASSERT_TRUE(hasLine(ui.display, "Spool not found"));
    press(ui, app, UiAction::Back);
    TEST_ASSERT_TRUE(ui.display.view().stale);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(UiAction::None), static_cast<int>(ui.display.view().left));
}

void test_archived_missing_and_filament_only_never_offer_load() {
    FakeReader reader;
    FakeServices services;
    FakeUi ui;
    FakeClock clock;
    App app(reader, services, ui, clock);
    app.tick();
    services.scan_ok = false;
    services.scan_error = ApiError::ArchivedSpool;
    present(reader, app);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(UiScreen::Error), static_cast<int>(ui.display.view().screen));
    press(ui, app, UiAction::Load);
    TEST_ASSERT_EQUAL_INT(0, services.map_calls);
    reader.present = false;
    app.tick();
    press(ui, app, UiAction::Back);
    services.scan_error = ApiError::MissingSpool;
    present(reader, app);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(UiScreen::Error), static_cast<int>(ui.display.view().screen));
    reader.present = false;
    app.tick();
    press(ui, app, UiAction::Back);
    services.scan_ok = true;
    services.scan_result.kind = ScanKind::FilamentOnly;
    present(reader, app);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(UiScreen::Tag), static_cast<int>(ui.display.view().screen));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(UiAction::None), static_cast<int>(ui.display.view().left));
    press(ui, app, UiAction::Load);
    TEST_ASSERT_EQUAL_INT(0, services.map_calls);
}

void test_rejected_and_uncertain_writes_show_error_then_fresh_mapping() {
    for (ApiError failure : {ApiError::Http, ApiError::Conflict, ApiError::Timeout}) {
        FakeReader reader;
        FakeServices services;
        services.server_spool = 7;
        services.map_ok = false;
        services.map_error = failure;
        FakeUi ui;
        FakeClock clock;
        App app(reader, services, ui, clock);
        app.tick();
        present(reader, app);
        press(ui, app, UiAction::Load);
        press(ui, app, UiAction::Confirm);
        TEST_ASSERT_EQUAL_INT(1, services.map_calls);
        TEST_ASSERT_EQUAL_INT(static_cast<int>(UiScreen::Error), static_cast<int>(ui.display.view().screen));
        TEST_ASSERT_TRUE(services.mapping_calls >= 3);
        press(ui, app, UiAction::Back);
        TEST_ASSERT_EQUAL_INT(static_cast<int>(UiScreen::Home), static_cast<int>(ui.display.view().screen));
        TEST_ASSERT_TRUE(hasLine(ui.display, "Spool 7"));
        press(ui, app, UiAction::Confirm);
        TEST_ASSERT_EQUAL_INT(1, services.map_calls);
    }
}

void test_settings_change_clears_preview_and_blocks_actions_until_refresh() {
    FakeReader reader;
    FakeServices services;
    services.server_spool = 7;
    FakeUi ui;
    FakeClock clock;
    App app(reader, services, ui, clock);
    app.tick();
    present(reader, app);
    app.settingsChanged();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(UiScreen::Home), static_cast<int>(ui.display.view().screen));
    TEST_ASSERT_TRUE(ui.display.view().stale);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(UiAction::None), static_cast<int>(ui.display.view().left));
    services.server_spool = 8;
    press(ui, app, UiAction::Load);
    TEST_ASSERT_EQUAL_INT(0, services.map_calls);
    TEST_ASSERT_FALSE(ui.display.view().stale);
    TEST_ASSERT_TRUE(hasLine(ui.display, "Spool 8"));
    ui.next = UiAction::Unload;
    app.tick();
    TEST_ASSERT_EQUAL_INT(0, services.map_calls);
}

void test_deferred_web_setting_change_applies_at_next_tick() {
    FakeReader reader;
    FakeServices services;
    services.server_spool = 7;
    FakeUi ui;
    ui.display.setPrinterName("Old printer");
    FakeClock clock;
    Settings settings;
    bool pending = false;
    App app(reader, services, ui, clock, nullptr, &settings, &pending);
    app.tick();
    present(reader, app);
    pending = true;
    services.server_spool = 8;
    app.tick();
    TEST_ASSERT_FALSE(pending);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(UiScreen::Home), static_cast<int>(ui.display.view().screen));
    TEST_ASSERT_EQUAL_STRING("Core One", ui.display.view().title.c_str());
    TEST_ASSERT_TRUE(hasLine(ui.display, "Spool 8"));
    TEST_ASSERT_EQUAL_INT(0, services.map_calls);
}
} // namespace

void setup() {
    Serial.begin(115200);
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_scan_requires_load_and_confirmation_before_mapping);
    RUN_TEST(test_unload_names_current_spool_and_maps_zero_only_after_confirmation);
    RUN_TEST(test_cancel_never_posts_for_load_or_unload);
    RUN_TEST(test_changed_mapping_requires_new_load_confirmation);
    RUN_TEST(test_idle_refresh_runs_at_thirty_seconds_only_on_home);
    RUN_TEST(test_back_requires_tag_removal_before_rescan);
    RUN_TEST(test_return_to_home_refreshes_mapping_immediately);
    RUN_TEST(test_working_screen_ignores_new_scan);
    RUN_TEST(test_failed_refresh_marks_home_stale_and_hides_unload);
    RUN_TEST(test_failed_spool_refresh_keeps_last_confirmed_details_stale);
    RUN_TEST(test_missing_current_spool_shows_specific_error_and_disables_unload);
    RUN_TEST(test_archived_missing_and_filament_only_never_offer_load);
    RUN_TEST(test_rejected_and_uncertain_writes_show_error_then_fresh_mapping);
    RUN_TEST(test_settings_change_clears_preview_and_blocks_actions_until_refresh);
    RUN_TEST(test_deferred_web_setting_change_applies_at_next_tick);
    UNITY_END();
}

void loop() {}
