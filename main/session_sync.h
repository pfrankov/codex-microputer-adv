#pragma once

#include <cstdint>

namespace session_sync {

enum class Method : uint8_t {
    LightingConfig = 1 << 0,
    ThreadStatus   = 1 << 1,
    DeviceStatus   = 1 << 2,
};

// Codex often republishes the real six-slot deck immediately after
// device.status. That follow-up is still the restored lamps, not six events.
constexpr uint32_t kHandshakeSettleMs = 1000;

class Tracker {
public:
    void begin()
    {
        mask_ = 0;
        hold_until_ms_ = 0;
    }

    void note(Method method, uint32_t now_ms = 0)
    {
        constexpr uint8_t complete = static_cast<uint8_t>(Method::LightingConfig)
                                   | static_cast<uint8_t>(Method::ThreadStatus)
                                   | static_cast<uint8_t>(Method::DeviceStatus);
        const bool was_incomplete = (mask_ & complete) != complete;
        mask_ |= static_cast<uint8_t>(method);
        if (was_incomplete && (mask_ & complete) == complete)
            hold_until_ms_ = now_ms + kHandshakeSettleMs;
    }

    bool baseline(uint32_t now_ms = 0) const
    {
        constexpr uint8_t complete = static_cast<uint8_t>(Method::LightingConfig)
                                   | static_cast<uint8_t>(Method::ThreadStatus)
                                   | static_cast<uint8_t>(Method::DeviceStatus);
        if ((mask_ & complete) != complete)
            return true;
        return static_cast<int32_t>(hold_until_ms_ - now_ms) > 0;
    }

private:
    uint8_t mask_ = 0;
    uint32_t hold_until_ms_ = 0;
};

}  // namespace session_sync
