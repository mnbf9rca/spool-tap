#include <Arduino.h>
#include <unity.h>

#include "ui.hpp"

namespace {
SpoolInfo spool(uint32_t id = 7) {
    SpoolInfo result;
    result.id = id;
    result.vendor = "Buddy3d";
    result.name = "Jet Black";
    result.material = "PLA";
    result.color_hex = "000000";
    result.remaining_g = 165.75;
    return result;
}

bool hasLine(const std::vector<std::string>& lines, const char* needle) {
    for (const auto& line : lines) if (line.find(needle) != std::string::npos) return true;
    return false;
}

bool validUtf8(const std::string& text) {
    for (size_t i = 0; i < text.size();) {
        const uint8_t first = static_cast<uint8_t>(text[i]);
        if (first < 0x80) { ++i; continue; }
        const size_t length = first >= 0xC2 && first <= 0xDF ? 2 :
                              first >= 0xE0 && first <= 0xEF ? 3 :
                              first >= 0xF0 && first <= 0xF4 ? 4 : 0;
        if (!length || i + length > text.size()) return false;
        for (size_t j = 1; j < length; ++j)
            if ((static_cast<uint8_t>(text[i + j]) & 0xC0) != 0x80) return false;
        i += length;
    }
    return true;
}

void test_loaded_home_shows_current_spool_status_and_unload() {
    Ui ui;
    ui.setConnections(true, true, true);
    SpoolInfo loaded = spool();
    Mapping mapping;
    mapping.spool_id = 7;
    ui.showHome(mapping, &loaded);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(UiScreen::Home), static_cast<int>(ui.view().screen));
    TEST_ASSERT_EQUAL_STRING("", ui.view().title.c_str());
    TEST_ASSERT_EQUAL_STRING("", ui.view().status.c_str());
    TEST_ASSERT_EQUAL_STRING("", ui.view().color_hex.c_str());
    TEST_ASSERT_TRUE(hasLine(ui.visibleLines(5), "Buddy3d PLA"));
    TEST_ASSERT_TRUE(hasLine(ui.visibleLines(5), "Jet Black"));
    TEST_ASSERT_TRUE(hasLine(ui.visibleLines(5), "166 g remaining"));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(UiAction::Unload), static_cast<int>(ui.actionAt(40, 210)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(UiAction::Status), static_cast<int>(ui.actionAt(220, 210)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(UiAction::None), static_cast<int>(ui.actionAt(40, 160)));
}

void test_home_activity_preserves_content_and_clears_on_next_screen() {
    Ui ui;
    ui.setConnections(true, true, true);
    Mapping mapping;
    SpoolInfo loaded = spool();
    mapping.spool_id = loaded.id;
    ui.showHome(mapping, &loaded);
    const auto before = ui.visibleLines(5);
    ui.setHomeActivity("Checking assignment...");
    TEST_ASSERT_EQUAL_INT(static_cast<int>(UiScreen::Home), static_cast<int>(ui.view().screen));
    TEST_ASSERT_EQUAL_STRING("Checking assignment...", ui.view().activity.c_str());
    TEST_ASSERT_EQUAL_STRING("", ui.view().status.c_str());
    TEST_ASSERT_TRUE(before == ui.visibleLines(5));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(UiAction::Status), static_cast<int>(ui.actionAt(220, 210)));
    ui.showHome(mapping, &loaded);
    TEST_ASSERT_EQUAL_STRING("", ui.view().activity.c_str());
}

void test_home_match_detects_only_visible_changes() {
    Ui ui;
    ui.setConnections(true, true, true);
    Mapping mapping;
    SpoolInfo loaded = spool();
    mapping.spool_id = loaded.id;
    ui.showHome(mapping, &loaded);
    ui.setHomeActivity("Getting spool details...");
    TEST_ASSERT_TRUE(ui.homeMatches(mapping, &loaded));
    TEST_ASSERT_EQUAL_STRING("Getting spool details...", ui.view().activity.c_str());
    loaded.remaining_g = 100;
    TEST_ASSERT_FALSE(ui.homeMatches(mapping, &loaded));
    mapping.stale = true;
    TEST_ASSERT_FALSE(ui.homeMatches(mapping, &loaded));
}

void test_update_screen_reports_progress_and_failure_without_mapping_action() {
    Ui ui;
    ui.showUpdateProgress(131072);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(UiScreen::Update), static_cast<int>(ui.view().screen));
    TEST_ASSERT_TRUE(hasLine(ui.visibleLines(5), "128 KB received"));
    TEST_ASSERT_TRUE(hasLine(ui.visibleLines(5), "Keep power on"));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(UiAction::None), static_cast<int>(ui.actionAt(40, 210)));
    ui.showUpdateResult(false);
    TEST_ASSERT_TRUE(hasLine(ui.visibleLines(5), "Current image"));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(UiAction::Back), static_cast<int>(ui.actionAt(220, 210)));
    ui.showUpdateResult(true);
    TEST_ASSERT_TRUE(hasLine(ui.visibleLines(5), "Rebooting"));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(UiAction::None), static_cast<int>(ui.actionAt(220, 210)));
}

void test_home_drop_order_and_unknown_remaining() {
    Ui ui;
    SpoolInfo loaded = spool();
    loaded.remaining_g = -1;
    Mapping mapping;
    mapping.spool_id = 7;
    ui.showHome(mapping, &loaded);
    const auto six = ui.visibleLines(6);
    const auto five = ui.visibleLines(5);
    const auto four = ui.visibleLines(4);
    TEST_ASSERT_FALSE(hasLine(six, "LOADED SPOOL"));
    TEST_ASSERT_TRUE(hasLine(six, "Tap reel"));
    TEST_ASSERT_FALSE(hasLine(five, "LOADED SPOOL"));
    TEST_ASSERT_TRUE(hasLine(five, "Tap reel"));
    TEST_ASSERT_EQUAL_STRING(" ", five[five.size() - 2].c_str());
    TEST_ASSERT_EQUAL_STRING("Tap reel to load", five.back().c_str());
    TEST_ASSERT_FALSE(hasLine(four, "Tap reel"));
    TEST_ASSERT_TRUE(hasLine(four, "Remaining unknown"));
}

void test_empty_or_stale_home_cannot_unload() {
    Ui ui;
    ui.setConnections(true, true, true);
    Mapping mapping;
    ui.showHome(mapping, nullptr);
    TEST_ASSERT_TRUE(hasLine(ui.visibleLines(4), "No spool loaded"));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(UiAction::None), static_cast<int>(ui.actionAt(40, 210)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(UiAction::Status), static_cast<int>(ui.actionAt(220, 210)));
    SpoolInfo loaded = spool();
    mapping.spool_id = 7;
    mapping.stale = true;
    ui.showHome(mapping, &loaded);
    TEST_ASSERT_TRUE(ui.view().stale);
    TEST_ASSERT_EQUAL_STRING("FilaBridge unavailable", ui.view().status.c_str());
    TEST_ASSERT_TRUE(hasLine(ui.visibleLines(4), "Last known spool:"));
    TEST_ASSERT_FALSE(hasLine(ui.visibleLines(5), "Tap reel"));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(UiAction::None), static_cast<int>(ui.actionAt(40, 210)));
}

void test_connection_changes_update_visible_home_status() {
    Ui ui;
    Mapping mapping;
    ui.showHome(mapping, nullptr);
    ui.setConnections(true, true, true);
    TEST_ASSERT_EQUAL_STRING("", ui.view().status.c_str());
    TEST_ASSERT_TRUE(ui.view().wifi);
    TEST_ASSERT_TRUE(ui.view().nfc);
    TEST_ASSERT_TRUE(ui.view().bridge);
    ui.setConnections(false, true, false);
    TEST_ASSERT_EQUAL_STRING("Wi-Fi disconnected", ui.view().status.c_str());
    TEST_ASSERT_FALSE(ui.view().wifi);
    TEST_ASSERT_TRUE(ui.view().nfc);
    TEST_ASSERT_FALSE(ui.view().bridge);
    ui.setConnections(true, false, false);
    TEST_ASSERT_EQUAL_STRING("NFC reader unavailable", ui.view().status.c_str());
    ui.setConnections(true, true, false);
    TEST_ASSERT_EQUAL_STRING("FilaBridge unavailable", ui.view().status.c_str());
    mapping.stale = true;
    ui.showHome(mapping, nullptr);
    ui.setConnections(true, true, true);
    TEST_ASSERT_FALSE(ui.view().bridge);
    TEST_ASSERT_EQUAL_STRING("FilaBridge unavailable", ui.view().status.c_str());
    ui.showHome(mapping, nullptr, ApiError::Timeout);
    TEST_ASSERT_EQUAL_STRING("Spoolman unavailable", ui.view().status.c_str());
    ui.setConnections(true, true, true);
    TEST_ASSERT_EQUAL_STRING("Spoolman unavailable", ui.view().status.c_str());
    ui.showError(ApiError::Timeout, "Tag lookup failed");
    TEST_ASSERT_EQUAL_STRING("Spoolman unavailable", ui.view().status.c_str());
    TEST_ASSERT_TRUE(hasLine(ui.visibleLines(5), "Tag lookup failed"));
    TEST_ASSERT_TRUE(hasLine(ui.visibleLines(5), "Back to retry"));
}

void test_linked_tag_preview_offers_load_and_cancel() {
    Ui ui;
    TagRead tag;
    tag.uid = "E0040150ABCD1234";
    tag.type = "ICODE SLIX2";
    ScanMatch match;
    match.kind = ScanKind::Spool;
    match.spool_id = 7;
    match.spool = spool();
    SpoolInfo current = spool(1);
    current.name = "Already loaded";
    ui.showTag(tag, {}, match, &current);
    TEST_ASSERT_TRUE(hasLine(ui.visibleLines(5), "Current: Already"));
    TEST_ASSERT_TRUE(hasLine(ui.visibleLines(5), "Jet Black"));
    TEST_ASSERT_TRUE(hasLine(ui.visibleLines(5), "166 g remaining"));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(UiAction::Load), static_cast<int>(ui.actionAt(40, 210)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(UiAction::Cancel), static_cast<int>(ui.actionAt(220, 210)));
}

void test_unknown_tag_shows_uid_type_hints_and_back_only() {
    Ui ui;
    TagRead tag;
    tag.uid = "E0040150ABCD1234";
    tag.type = "ICODE SLIX2";
    TagHints hints;
    hints.brand = "Prusament";
    hints.material = "PETG";
    ScanMatch match;
    ui.showTag(tag, hints, match, nullptr);
    TEST_ASSERT_EQUAL_STRING("No spool linked", ui.view().title.c_str());
    TEST_ASSERT_TRUE(hasLine(ui.visibleLines(5), "E0040150ABCD1234"));
    TEST_ASSERT_FALSE(hasLine(ui.visibleLines(5), "ICODE SLIX2"));
    TEST_ASSERT_TRUE(hasLine(ui.visibleLines(5), "Prusament PETG"));
    TEST_ASSERT_TRUE(hasLine(ui.visibleLines(5), "Assign in Spoolman"));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(UiAction::None), static_cast<int>(ui.actionAt(40, 210)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(UiAction::Back), static_cast<int>(ui.actionAt(220, 210)));
}

void test_filament_only_prompts_for_spool_assignment_without_load() {
    Ui ui;
    ScanMatch match;
    match.kind = ScanKind::FilamentOnly;
    TagRead tag;
    tag.uid = "A1B2";
    ui.showTag(tag, {}, match, nullptr);
    TEST_ASSERT_EQUAL_STRING("Filament found", ui.view().title.c_str());
    TEST_ASSERT_TRUE(hasLine(ui.visibleLines(5), "No spool linked"));
    TEST_ASSERT_TRUE(hasLine(ui.visibleLines(5), "Assign in Spoolman"));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(UiAction::None), static_cast<int>(ui.actionAt(40, 210)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(UiAction::Back), static_cast<int>(ui.actionAt(220, 210)));
}

void test_archived_and_missing_spools_have_specific_no_load_views() {
    Ui ui;
    ScanMatch match;
    match.kind = ScanKind::Spool;
    match.spool = spool();
    match.spool.archived = true;
    TagRead tag;
    ui.showTag(tag, {}, match, nullptr);
    TEST_ASSERT_EQUAL_STRING("Spool archived", ui.view().title.c_str());
    TEST_ASSERT_TRUE(hasLine(ui.visibleLines(5), "Cannot load"));
    TEST_ASSERT_EQUAL_STRING("", ui.view().status.c_str());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(UiAction::None), static_cast<int>(ui.actionAt(40, 210)));
    ui.showError(ApiError::MissingSpool);
    TEST_ASSERT_EQUAL_STRING("Spool not found", ui.view().title.c_str());
    TEST_ASSERT_TRUE(hasLine(ui.visibleLines(5), "Check in Spoolman"));
    ui.showError(ApiError::InvalidTag);
    TEST_ASSERT_TRUE(hasLine(ui.visibleLines(5), "Tag UID"));
    TEST_ASSERT_TRUE(hasLine(ui.visibleLines(5), "unreadable"));
}

void test_unreadable_ndef_keeps_linked_spool_loadable() {
    Ui ui;
    TagRead tag;
    tag.ndef_malformed = true;
    ScanMatch match;
    match.kind = ScanKind::Spool;
    match.spool_id = 7;
    match.spool = spool();
    SpoolInfo current = spool(1);
    current.name = "Already loaded";
    ui.showTag(tag, {}, match, &current);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(UiScreen::Tag), static_cast<int>(ui.view().screen));
    TEST_ASSERT_TRUE(hasLine(ui.visibleLines(5), "Jet Black"));
    TEST_ASSERT_TRUE(hasLine(ui.visibleLines(5), "Tag data"));
    TEST_ASSERT_TRUE(hasLine(ui.visibleLines(5), "unreadable"));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(UiAction::Load), static_cast<int>(ui.actionAt(40, 210)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(UiAction::Cancel), static_cast<int>(ui.actionAt(220, 210)));
}

void test_unreadable_ndef_without_spool_prompts_for_assignment() {
    Ui ui;
    TagRead tag;
    tag.uid = "E0040150ABCD1234";
    tag.ndef_malformed = true;
    TagHints hints;
    hints.brand = "Ignore this";
    ui.showTag(tag, hints, {}, nullptr);
    TEST_ASSERT_EQUAL_STRING("No spool linked", ui.view().title.c_str());
    TEST_ASSERT_TRUE(hasLine(ui.visibleLines(5), "E0040150ABCD1234"));
    TEST_ASSERT_TRUE(hasLine(ui.visibleLines(5), "Tag data"));
    TEST_ASSERT_TRUE(hasLine(ui.visibleLines(5), "unreadable"));
    TEST_ASSERT_FALSE(hasLine(ui.visibleLines(5), "Ignore this"));
    TEST_ASSERT_TRUE(hasLine(ui.visibleLines(5), "Assign in"));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(UiAction::None), static_cast<int>(ui.actionAt(40, 210)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(UiAction::Back), static_cast<int>(ui.actionAt(220, 210)));
}

void test_working_confirmation_status_and_touch_bounds() {
    Ui ui;
    ui.setConnections(false, true, false);
    ui.showWorking("Checking tag");
    TEST_ASSERT_EQUAL_INT(static_cast<int>(UiScreen::Working), static_cast<int>(ui.view().screen));
    TEST_ASSERT_EQUAL_STRING("Checking tag", ui.view().title.c_str());
    TEST_ASSERT_EQUAL_STRING("Wi-Fi disconnected", ui.view().status.c_str());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(UiAction::None), static_cast<int>(ui.actionAt(40, 210)));
    SpoolInfo loaded = spool();
    ui.showConfirm(UiAction::Unload, loaded);
    TEST_ASSERT_TRUE(hasLine(ui.visibleLines(5), "Jet Black"));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(UiAction::Confirm), static_cast<int>(ui.actionAt(40, 210)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(UiAction::Cancel), static_cast<int>(ui.actionAt(220, 210)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(UiAction::None), static_cast<int>(ui.actionAt(-1, 210)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(UiAction::None), static_cast<int>(ui.actionAt(320, 210)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(UiAction::None), static_cast<int>(ui.actionAt(40, 183)));
    ui.showStatus("192.168.1.5", "v1");
    TEST_ASSERT_TRUE(hasLine(ui.visibleLines(6), "192.168.1.5"));
    TEST_ASSERT_TRUE(hasLine(ui.visibleLines(6), "/settings"));
    TEST_ASSERT_TRUE(hasLine(ui.visibleLines(6), "/logs"));
    TEST_ASSERT_TRUE(hasLine(ui.visibleLines(6), "/update"));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(UiAction::Back), static_cast<int>(ui.actionAt(220, 210)));
}

void test_long_and_untrusted_tag_text_is_wrapped_and_sanitized() {
    Ui ui;
    TagRead tag;
    tag.uid = "A1B2";
    tag.type = "NTAG 215";
    TagHints hints;
    hints.brand = "ABCDEFGHIJKLMNO PQRSTUV";
    hints.material = std::string("PETG\n") + static_cast<char>(0xFF);
    ui.showTag(tag, hints, {}, nullptr);
    const auto lines = ui.visibleLines(6);
    TEST_ASSERT_TRUE(hasLine(lines, "ABCDEFGHIJKLMNO"));
    TEST_ASSERT_TRUE(hasLine(lines, "PQRSTUV"));
    for (const auto& line : lines) {
        TEST_ASSERT_TRUE(line.size() <= 18);
        TEST_ASSERT_TRUE(line.find('\n') == std::string::npos);
        TEST_ASSERT_TRUE(line.find(static_cast<char>(0xFF)) == std::string::npos);
    }
}

void test_wrapping_does_not_split_a_utf8_character() {
    Ui ui;
    TagRead tag;
    tag.uid = "A1B2";
    tag.type = "NTAG";
    TagHints hints;
    hints.brand = std::string(17, 'X') + " ABCDEFGHIJKLM\xC3\xA9" "BCMORE";
    ui.showTag(tag, hints, {}, nullptr);
    for (const auto& line : ui.visibleLines(6)) TEST_ASSERT_TRUE(validUtf8(line));
}

void test_open_tag_hints_are_available_when_screen_has_space() {
    Ui ui;
    TagRead tag;
    tag.uid = "E0040150ABCD1234";
    tag.type = "ICODE SLIX2";
    TagHints hints;
    hints.brand = "Prusament";
    hints.material_type = "PETG";
    hints.material = "Jet Black";
    hints.color_hex = "101010";
    hints.identifier = "GTIN 12345";
    hints.nominal_weight_g = 1000;
    hints.min_temp_c = "230";
    hints.max_temp_c = "250";
    ui.showTag(tag, hints, {}, nullptr);
    const auto lines = ui.visibleLines(11);
    TEST_ASSERT_TRUE(hasLine(lines, "Prusament PETG"));
    TEST_ASSERT_TRUE(hasLine(lines, "Jet Black"));
    TEST_ASSERT_TRUE(hasLine(lines, "GTIN 12345"));
    TEST_ASSERT_TRUE(hasLine(lines, "Nominal 1000 g"));
    TEST_ASSERT_TRUE(hasLine(lines, "230-250C"));
    TEST_ASSERT_EQUAL_STRING("101010", ui.view().color_hex.c_str());
    const auto screen_lines = ui.visibleLines(5);
    TEST_ASSERT_TRUE(hasLine(screen_lines, "E0040150ABCD1234"));
    TEST_ASSERT_TRUE(hasLine(screen_lines, "Prusament PETG"));
    TEST_ASSERT_TRUE(hasLine(screen_lines, "Nominal 1000 g"));
    TEST_ASSERT_TRUE(hasLine(screen_lines, "Assign in Spoolman"));
    TEST_ASSERT_FALSE(hasLine(screen_lines, "ICODE SLIX2"));
}

void test_missing_optional_spool_names_do_not_consume_a_row() {
    Ui ui;
    SpoolInfo loaded = spool();
    loaded.vendor.clear();
    loaded.name.clear();
    loaded.material.clear();
    Mapping mapping;
    mapping.spool_id = loaded.id;
    ui.showHome(mapping, &loaded);
    TEST_ASSERT_EQUAL_INT(3, static_cast<int>(ui.visibleLines(4).size()));
    TEST_ASSERT_TRUE(hasLine(ui.visibleLines(4), "166 g remaining"));
    ScanMatch match;
    match.kind = ScanKind::Spool;
    match.spool_id = loaded.id;
    match.spool = loaded;
    ui.showTag({}, {}, match, nullptr);
    for (const auto& line : ui.visibleLines(5)) TEST_ASSERT_FALSE(line.find_first_not_of(' ') == std::string::npos);
}
} // namespace

void setup() {
    Serial.begin(115200);
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_update_screen_reports_progress_and_failure_without_mapping_action);
    RUN_TEST(test_loaded_home_shows_current_spool_status_and_unload);
    RUN_TEST(test_home_activity_preserves_content_and_clears_on_next_screen);
    RUN_TEST(test_home_match_detects_only_visible_changes);
    RUN_TEST(test_home_drop_order_and_unknown_remaining);
    RUN_TEST(test_empty_or_stale_home_cannot_unload);
    RUN_TEST(test_connection_changes_update_visible_home_status);
    RUN_TEST(test_linked_tag_preview_offers_load_and_cancel);
    RUN_TEST(test_unknown_tag_shows_uid_type_hints_and_back_only);
    RUN_TEST(test_filament_only_prompts_for_spool_assignment_without_load);
    RUN_TEST(test_archived_and_missing_spools_have_specific_no_load_views);
    RUN_TEST(test_unreadable_ndef_keeps_linked_spool_loadable);
    RUN_TEST(test_unreadable_ndef_without_spool_prompts_for_assignment);
    RUN_TEST(test_working_confirmation_status_and_touch_bounds);
    RUN_TEST(test_long_and_untrusted_tag_text_is_wrapped_and_sanitized);
    RUN_TEST(test_wrapping_does_not_split_a_utf8_character);
    RUN_TEST(test_open_tag_hints_are_available_when_screen_has_space);
    RUN_TEST(test_missing_optional_spool_names_do_not_consume_a_row);
    UNITY_END();
}

void loop() {}
