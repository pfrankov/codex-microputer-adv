#include <cstdlib>
#include <iostream>

#include "status_reducer.h"

model::State model::state;

namespace {

int failures = 0;

void check(bool condition, const char* name)
{
    if (condition)
        return;
    ++failures;
    std::cerr << "FAIL status_reducer: " << name << '\n';
}

status_reducer::LampFrame lamp(uint32_t color, uint8_t effect = 1, float brightness = 1.f)
{
    return {color, brightness, 0.f, effect};
}

void initial_sync_uses_lamp_color_without_event()
{
    model::Task viewed;
    auto result = status_reducer::apply(viewed, lamp(0xffffff));
    check(result.initial_sync, "viewed initial frame is sync");
    check(!result.changed, "viewed initial frame has no event");
    check(viewed.status == model::Status::Done, "viewed initial status is done");
    check(!viewed.unseen_done, "white initial lamp is gray/viewed");
    check(!viewed.completion_hold, "initial done has no permanent hold");

    model::Task restored;
    result = status_reducer::apply(restored, lamp(0x00ff4c));
    check(!restored.unseen_done, "green initial lamp is restored as viewed");
    check(restored.locally_viewed_done, "baseline completion resists stale green snapshots");
    check(!result.changed, "green initial frame has no event");
}

void multi_frame_session_baseline_never_becomes_an_event()
{
    model::Task task;
    status_reducer::apply(task, lamp(0x304ffe), true);
    auto result = status_reducer::apply(task, lamp(0xff6d00), true);
    check(result.initial_sync, "second baseline frame is still sync");
    check(!result.changed, "second baseline frame has no event");
    result = status_reducer::apply(task, lamp(0xffffff), true);
    check(!result.changed, "final baseline frame has no event");
    check(!task.completion_hold, "baseline completion has no local hold");
    check(!task.unseen_done, "white baseline completion is viewed");
}

void handshake_follow_up_does_not_replay_every_slot()
{
    model::Task tasks[6];
    const uint32_t restored[6] = {
        lamp::kRunning, lamp::kNeedsInput, lamp::kDoneUnseen,
        lamp::kDoneSeen, lamp::kError, lamp::kRunning,
    };
    for (int i = 0; i < 6; ++i)
        status_reducer::apply(tasks[i], lamp(0xffffff), true);
    for (int i = 0; i < 6; ++i) {
        auto result = status_reducer::apply(tasks[i], lamp(restored[i]), true);
        check(!result.changed, "restored deck after handshake is not an event");
    }
}

void fresh_completion_holds_green_until_animation_finishes()
{
    model::Task task;
    status_reducer::apply(task, lamp(0x304ffe));
    auto result = status_reducer::apply(task, lamp(0xffffff));
    check(result.changed, "running to done is an event");
    check(result.target_unseen == false, "white lamp target is viewed");
    check(task.unseen_done, "fresh completion is green during animation");
    check(task.completion_hold, "fresh completion owns temporary hold");
}

void repeated_and_restored_frames_are_silent()
{
    model::Task task;
    status_reducer::apply(task, lamp(0x304ffe));
    auto same = status_reducer::apply(task, lamp(0x304ffe));
    check(!same.changed, "identical running frame is silent");

    auto off = status_reducer::apply(task, lamp(0x304ffe, 0, 0.f));
    check(!off.changed, "off presentation frame is silent");
    check(task.status == model::Status::Running, "off frame preserves semantic status");

    auto restored = status_reducer::apply(task, lamp(0x304ffe));
    check(restored.restoration, "on frame is identified as restoration");
    check(!restored.changed, "restoration does not replay event");
}

void latest_lamp_color_remains_authoritative()
{
    model::Task task;
    status_reducer::apply(task, lamp(0x304ffe));
    status_reducer::apply(task, lamp(0x00ff4c));
    task.completion_hold = false; // animation finalizer
    status_reducer::apply(task, lamp(0x00ff4c));
    check(task.unseen_done, "green remains green after animation");
    status_reducer::apply(task, lamp(0xffffff));
    check(!task.unseen_done, "white becomes viewed after hold release");
}

void selected_completion_stays_viewed_despite_stale_green_lamp()
{
    model::state = model::State{};
    model::state.task_count = 1;
    auto& task = model::state.tasks[0];
    status_reducer::apply(task, lamp(0x304ffe));
    status_reducer::apply(task, lamp(0x00ff4c));
    model::mark_done_viewed(0);
    check(task.unseen_done, "selected fresh completion stays green during animation");
    check(task.locally_viewed_done, "selected completion records local view");

    task.completion_hold = false; // animation finalizer
    status_reducer::apply(task, lamp(0x00ff4c));
    check(!task.unseen_done, "stale green frame cannot revive viewed completion");

    status_reducer::apply(task, lamp(0x304ffe));
    check(!task.locally_viewed_done, "new work resets local viewed latch");
}

void selecting_existing_unread_completion_marks_it_viewed()
{
    model::state = model::State{};
    model::state.task_count = 1;
    auto& task = model::state.tasks[0];
    status_reducer::apply(task, lamp(0x304ffe));
    status_reducer::apply(task, lamp(0x00ff4c));
    task.completion_hold = false; // background completion animation finished
    status_reducer::apply(task, lamp(0x00ff4c));
    check(task.unseen_done, "background active-to-done transition starts unread");
    model::mark_done_viewed(0);
    check(!task.unseen_done, "selecting unread completion marks it viewed");
}

void selected_completion_settles_after_animation_ownership_ends()
{
    model::Task task;
    task.status = model::Status::Done;
    task.unseen_done = true;
    task.completion_hold = true;
    task.locally_viewed_done = true;

    check(!model::settle_viewed_completion(task, true),
          "active or queued animation retains completion hold");
    check(task.unseen_done && task.completion_hold,
          "owned completion remains green");
    check(model::settle_viewed_completion(task, false),
          "unowned viewed completion settles");
    check(!task.unseen_done && !task.completion_hold,
          "settled completion is gray with no hold");
}

} // namespace

int main()
{
    initial_sync_uses_lamp_color_without_event();
    multi_frame_session_baseline_never_becomes_an_event();
    handshake_follow_up_does_not_replay_every_slot();
    fresh_completion_holds_green_until_animation_finishes();
    repeated_and_restored_frames_are_silent();
    latest_lamp_color_remains_authoritative();
    selected_completion_stays_viewed_despite_stale_green_lamp();
    selecting_existing_unread_completion_marks_it_viewed();
    selected_completion_settles_after_animation_ownership_ends();
    if (failures)
        return EXIT_FAILURE;
    std::cout << "PASS status_reducer (9 scenarios)\n";
    return EXIT_SUCCESS;
}
