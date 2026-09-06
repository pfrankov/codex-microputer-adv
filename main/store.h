#pragma once

// save_settings() only marks the settings dirty; service() commits them once
// they have stayed unchanged for the debounce window. Call flush() instead
// when persistence must land before a risky action (transport teardown,
// reboot), and check its result.
namespace store {
void init();
void save_settings();
void load_settings();
void service();
bool flush();
// Exact ESP-IDF error from the most recent failed open/write/commit. This is
// stable text suitable for the small on-device diagnostic toast.
const char* last_error_name();
} // namespace store
