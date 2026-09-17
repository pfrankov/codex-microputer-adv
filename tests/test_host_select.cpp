#include <cstdlib>
#include <initializer_list>
#include <iostream>

#include "host_select.h"

namespace {

int failures = 0;

void check(bool condition, const char* name)
{
    if (condition) return;
    ++failures;
    std::cerr << "FAIL host_select: " << name << '\n';
}

host_select::Slot lamp(uint32_t color, uint8_t effect = 1, float brightness = 1.f,
                       bool sync_keys = false)
{
    host_select::Slot slot;
    slot.color = color;
    slot.effect = effect;
    slot.brightness = brightness;
    slot.sync_keys = sync_keys;
    host_select::refresh_present(slot);
    return slot;
}

host_select::Snapshot deck(std::initializer_list<host_select::Slot> slots)
{
    host_select::Snapshot snap;
    int i = 0;
    for (const auto& slot : slots) {
        if (i < 6) snap.slots[i++] = slot;
    }
    return snap;
}

void white_blink_is_the_selected_idle_chat()
{
    auto now = deck({
        lamp(lamp::kRunning, host_select::kBreath),
        lamp(lamp::kDoneSeen, host_select::kBreath),
        lamp(lamp::kNeedsInput),
        lamp(lamp::kDoneUnseen),
        lamp(lamp::kRunning, host_select::kBreath),
        lamp(0, 0, 0.f),
    });
    auto result = host_select::infer(now);
    check(result.slot == 1, "white blink wins over working breaths");
    check(result.reason == host_select::Reason::WhiteBlink, "reason is white blink");
}

void unique_breath_selects_a_running_chat()
{
    auto now = deck({
        lamp(lamp::kDoneSeen),
        lamp(lamp::kRunning, host_select::kBreath),
        lamp(lamp::kNeedsInput),
        lamp(lamp::kDoneUnseen),
        lamp(lamp::kError),
        lamp(0, 0, 0.f),
    });
    auto result = host_select::infer(now);
    check(result.slot == 1, "unique breath selects the running slot");
    check(result.reason == host_select::Reason::UniqueBlink, "reason is unique blink");
}

void shallow_breath_counts_as_selection()
{
    auto now = deck({
        lamp(lamp::kDoneSeen, host_select::kShallowBreath),
        lamp(lamp::kRunning),
        lamp(lamp::kNeedsInput),
        lamp(lamp::kDoneUnseen),
        lamp(lamp::kError),
        lamp(0, 0, 0.f),
    });
    auto result = host_select::infer(now);
    check(result.slot == 0, "shallow breath is a blink");
    check(result.reason == host_select::Reason::WhiteBlink, "white shallow breath is selected");
}

void unique_sync_keys_selects_without_breath()
{
    auto now = deck({
        lamp(lamp::kDoneSeen),
        lamp(lamp::kDoneSeen),
        lamp(lamp::kRunning, host_select::kSolid, 1.f, true),
        lamp(lamp::kNeedsInput),
        lamp(lamp::kError),
        lamp(0, 0, 0.f),
    });
    auto result = host_select::infer(now);
    check(result.slot == 2, "unique sk selects a solid running chat");
    check(result.reason == host_select::Reason::SyncKeys, "reason is sync keys");
}

void gained_blink_follows_a_chat_switch()
{
    auto prev = deck({
        lamp(lamp::kDoneSeen, host_select::kBreath),
        lamp(lamp::kRunning),
        lamp(lamp::kNeedsInput),
        lamp(lamp::kDoneUnseen),
        lamp(lamp::kError),
        lamp(0, 0, 0.f),
    });
    auto now = deck({
        lamp(lamp::kDoneSeen),
        lamp(lamp::kRunning, host_select::kBreath),
        lamp(lamp::kNeedsInput),
        lamp(lamp::kDoneUnseen),
        lamp(lamp::kError),
        lamp(0, 0, 0.f),
    });
    auto result = host_select::infer(now, &prev);
    check(result.slot == 1, "the slot that gained breath is selected");
    check(result.reason == host_select::Reason::GainedBlink
              || result.reason == host_select::Reason::UniqueBlink,
          "gained or unique blink after the move");
}

void viewed_edge_selects_an_opened_completion()
{
    auto prev = deck({
        lamp(lamp::kDoneSeen, host_select::kBreath),
        lamp(lamp::kDoneUnseen),
        lamp(lamp::kRunning),
        lamp(lamp::kNeedsInput),
        lamp(lamp::kError),
        lamp(0, 0, 0.f),
    });
    auto now = deck({
        lamp(lamp::kDoneSeen),
        lamp(lamp::kDoneSeen),
        lamp(lamp::kRunning),
        lamp(lamp::kNeedsInput),
        lamp(lamp::kError),
        lamp(0, 0, 0.f),
    });
    auto result = host_select::infer(now, &prev);
    check(result.slot == 1, "green-to-white is the chat that was just opened");
    check(result.reason == host_select::Reason::Viewed, "reason is viewed");
}

void background_work_breath_does_not_steal_selection()
{
    auto prev = deck({
        lamp(lamp::kRunning, host_select::kBreath),
        lamp(lamp::kDoneSeen),
        lamp(lamp::kNeedsInput),
        lamp(lamp::kDoneUnseen),
        lamp(lamp::kError),
        lamp(0, 0, 0.f),
    });
    auto now = deck({
        lamp(lamp::kRunning, host_select::kBreath),
        lamp(lamp::kRunning, host_select::kBreath),
        lamp(lamp::kNeedsInput),
        lamp(lamp::kDoneUnseen),
        lamp(lamp::kError),
        lamp(0, 0, 0.f),
    });
    auto result = host_select::infer(now, &prev);
    check(result.slot < 0, "a new working breath is not a chat switch");
    check(result.reason == host_select::Reason::None, "background work has no selection reason");
}

void no_cue_keeps_the_current_cursor()
{
    auto now = deck({
        lamp(lamp::kDoneSeen),
        lamp(lamp::kDoneSeen),
        lamp(lamp::kRunning),
        lamp(lamp::kNeedsInput),
        lamp(lamp::kError),
        lamp(0, 0, 0.f),
    });
    auto result = host_select::infer(now);
    check(result.slot < 0, "solid lamps do not invent a selection");
    check(result.reason == host_select::Reason::None, "no selection reason");
}

void picker_colours_are_not_task_status()
{
    auto picker = deck({
        lamp(0x1b4fd0),
        lamp(0xe2451e),
        lamp(0x4cb949),
        lamp(0, 0, 0.f),
        lamp(0, 0, 0.f),
        lamp(0, 0, 0.f),
    });
    check(!host_select::looks_like_task_status(picker), "foreign colours are a picker");

    auto tasks = deck({
        lamp(lamp::kRunning, host_select::kBreath),
        lamp(lamp::kDoneSeen),
        lamp(lamp::kNeedsInput),
        lamp(lamp::kDoneUnseen),
        lamp(lamp::kError),
        lamp(0, 0, 0.f),
    });
    check(host_select::looks_like_task_status(tasks), "host lamp colours are task status");
}

void effect_names_round_trip()
{
    check(host_select::effect_named("breath", 1) == host_select::kBreath, "breath name");
    check(host_select::effect_named("shallow-breath", 1) == host_select::kShallowBreath,
          "shallow-breath name");
    check(host_select::effect_named("solid", 4) == host_select::kSolid, "solid name");
    check(host_select::effect_named("nope", 9) == 9, "unknown name keeps fallback");
}

}  // namespace

int main()
{
    white_blink_is_the_selected_idle_chat();
    unique_breath_selects_a_running_chat();
    shallow_breath_counts_as_selection();
    unique_sync_keys_selects_without_breath();
    gained_blink_follows_a_chat_switch();
    viewed_edge_selects_an_opened_completion();
    background_work_breath_does_not_steal_selection();
    no_cue_keeps_the_current_cursor();
    picker_colours_are_not_task_status();
    effect_names_round_trip();
    if (failures) return EXIT_FAILURE;
    std::cout << "PASS host_select (10 scenarios)\n";
    return EXIT_SUCCESS;
}
