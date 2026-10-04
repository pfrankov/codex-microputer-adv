// Keyboard input, abstracted over the two Cardputer generations.
//
// The original Cardputer scans a 7x8 GPIO matrix through a 74HC138 column
// decoder. The ADV puts a TCA8418 controller on the internal I2C bus. Both
// produce the same 4x14 logical layout, so only the scanner differs.
#pragma once

#include <cstdint>

namespace keys {

enum class Key : uint8_t {
    None, Up, Down, Left, Right, Enter, Back, Settings, DebugSettings,
    Digit, Record, Interrupt, Mute,
    // Reasoning depth and the model picker. Both are *relative*: the host only
    // accepts encoder detents and clicks, so there is no way to name a value.
    EncoderLeft, EncoderRight,
    // A host-configurable Codex Micro command slot ACT06..ACT11 or ACT10_ACT11.
    // Press::digit carries the numeric suffix, or 1011 for ACT10_ACT11.
    // ACT12 remains the Enter key.
    NativeAction,
    // The key map, on screen. Micro has to be memorised; this one does not.
    Help,
    // Any key with no action of its own. Still reported, because a dark panel
    // must wake on *any* key, not only on ones that happen to be mapped.
    Other,
};

struct Press {
    Key key   = Key::None;
    int digit = 0;
    bool down = true;
};

// The combined action has a wire identifier outside the ordinary slot range.
// Keep its held transport separate without using that wire value as an index.
constexpr int kNativeActionCount = 14;
constexpr int native_action_index(int slot)
{
    return slot == 1011 ? 13 : (slot >= 6 && slot <= 12 ? slot : -1);
}

enum class Backend : uint8_t { None, Matrix, Tca8418 };

// Chooses a backend from the detected board. Safe to call once at boot.
Backend init();
Backend backend();
const char* backend_name();

// Human-readable name, for telemetry and debugging.
const char* name(Key key);

// Returns the next pending press, or Key::None when the queue is empty.
// Arrow-style keys auto-repeat while held.
Press next();

}  // namespace keys
