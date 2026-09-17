#pragma once

#include <cstdint>
#include <cstring>

#include "lamp.h"

namespace host_select {

constexpr uint8_t kOff = 0;
constexpr uint8_t kSolid = 1;
constexpr uint8_t kBreath = 4;
constexpr uint8_t kShallowBreath = 6;

enum class Reason : uint8_t {
    None,
    WhiteBlink,
    SyncKeys,
    UniqueBlink,
    GainedBlink,
    Viewed,
};

struct Slot {
    bool present = false;
    uint32_t color = 0;
    float brightness = 0.f;
    uint8_t effect = 0;
    bool sync_keys = false;
};

struct Snapshot {
    Slot slots[6] = {};
};

struct Result {
    int slot = -1;
    Reason reason = Reason::None;
};

inline bool is_blink(uint8_t effect)
{
    return effect == kBreath || effect == kShallowBreath;
}

inline bool is_white(uint32_t color)
{
    return (color & 0xffffff) == lamp::kDoneSeen;
}

inline bool is_semantic_color(uint32_t color)
{
    color &= 0xffffff;
    return color == lamp::kRunning || color == lamp::kNeedsInput
        || color == lamp::kDoneSeen || color == lamp::kDoneUnseen
        || color == lamp::kError;
}

inline void refresh_present(Slot& slot)
{
    slot.present = slot.effect != 0 && slot.brightness > 0.001f;
}

inline uint8_t effect_named(const char* name, uint8_t fallback)
{
    if (!name) return fallback;
    if (std::strcmp(name, "off") == 0) return kOff;
    if (std::strcmp(name, "solid") == 0) return kSolid;
    if (std::strcmp(name, "snake") == 0) return 2;
    if (std::strcmp(name, "rainbow") == 0) return 3;
    if (std::strcmp(name, "breath") == 0) return kBreath;
    if (std::strcmp(name, "gradient") == 0) return 5;
    if (std::strcmp(name, "shallowBreath") == 0) return kShallowBreath;
    if (std::strcmp(name, "shallow-breath") == 0) return kShallowBreath;
    if (std::strcmp(name, "shallow_breath") == 0) return kShallowBreath;
    return fallback;
}

inline const char* reason_name(Reason reason)
{
    switch (reason) {
    case Reason::WhiteBlink: return "white_blink";
    case Reason::SyncKeys: return "sync_keys";
    case Reason::UniqueBlink: return "unique_blink";
    case Reason::GainedBlink: return "gained_blink";
    case Reason::Viewed: return "viewed";
    default: return "none";
    }
}

inline bool any_lit(const Snapshot& snap)
{
    for (int i = 0; i < 6; ++i)
        if (snap.slots[i].present) return true;
    return false;
}

inline float brightest(const Snapshot& snap)
{
    float value = 0.f;
    for (int i = 0; i < 6; ++i)
        if (snap.slots[i].brightness > value) value = snap.slots[i].brightness;
    return value;
}

// Picker previews use arbitrary UI colours. A live task snapshot only carries
// the five host lamp colours, so a foreign lit colour is the picker.
inline bool looks_like_task_status(const Snapshot& snap)
{
    int semantic = 0;
    for (int i = 0; i < 6; ++i) {
        if (!snap.slots[i].present) continue;
        if (!is_semantic_color(snap.slots[i].color)) return false;
        ++semantic;
    }
    return semantic > 0;
}

inline uint8_t blink_mask(const Snapshot& snap)
{
    uint8_t mask = 0;
    for (int i = 0; i < 6; ++i) {
        if (snap.slots[i].present && is_blink(snap.slots[i].effect))
            mask = static_cast<uint8_t>(mask | (1u << i));
    }
    return mask;
}

inline int bit_index(uint8_t mask)
{
    for (int i = 0; i < 6; ++i)
        if (mask & (1u << i)) return i;
    return -1;
}

inline int popcount6(uint8_t mask)
{
    int count = 0;
    for (int i = 0; i < 6; ++i)
        if (mask & (1u << i)) ++count;
    return count;
}

// Desktop encodes the selected chat as a white blink on the idle/viewed lamp,
// a unique breath/shallowBreath, or sk=1. Working lamps can also breathe, so
// a unique white blink outranks a crowd of blue breaths. When those cues are
// absent, a slot that just started blinking, or the only green-to-white viewed
// edge, is the chat the user just opened.
inline Result infer(const Snapshot& now, const Snapshot* prev = nullptr)
{
    Result result;
    int white_blink = -1, white_count = 0;
    int any_blink = -1, any_count = 0;
    int sync_slot = -1, sync_count = 0;
    for (int i = 0; i < 6; ++i) {
        const Slot& slot = now.slots[i];
        if (!slot.present) continue;
        if (is_blink(slot.effect)) {
            any_blink = i;
            ++any_count;
            if (is_white(slot.color)) {
                white_blink = i;
                ++white_count;
            }
        }
        if (slot.sync_keys) {
            sync_slot = i;
            ++sync_count;
        }
    }
    if (white_count == 1) {
        result.slot = white_blink;
        result.reason = Reason::WhiteBlink;
        return result;
    }
    if (sync_count == 1) {
        result.slot = sync_slot;
        result.reason = Reason::SyncKeys;
        return result;
    }
    if (any_count == 1) {
        result.slot = any_blink;
        result.reason = Reason::UniqueBlink;
        return result;
    }
    if (prev) {
        const uint8_t now_blinks = blink_mask(now);
        const uint8_t prev_blinks = blink_mask(*prev);
        const uint8_t gained = static_cast<uint8_t>(now_blinks & ~prev_blinks);
        const uint8_t lost = static_cast<uint8_t>(prev_blinks & ~now_blinks);
        // A background task starting work also gains a breath. That is not a
        // chat switch unless a blink also left the previous slot, or this is
        // the first blink on an otherwise solid deck.
        if (popcount6(gained) == 1 && (popcount6(lost) == 1 || prev_blinks == 0)) {
            result.slot = bit_index(gained);
            result.reason = Reason::GainedBlink;
            return result;
        }
        int viewed = -1, viewed_count = 0;
        for (int i = 0; i < 6; ++i) {
            if (!prev->slots[i].present || !now.slots[i].present) continue;
            if ((prev->slots[i].color & 0xffffff) == lamp::kDoneUnseen
                && (now.slots[i].color & 0xffffff) == lamp::kDoneSeen) {
                viewed = i;
                ++viewed_count;
            }
        }
        if (viewed_count == 1) {
            result.slot = viewed;
            result.reason = Reason::Viewed;
            return result;
        }
    }
    return result;
}

}  // namespace host_select
