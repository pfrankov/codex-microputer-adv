#include <cstdlib>
#include <iostream>

#include "session_sync.h"

namespace {
int failures = 0;
void check(bool condition, const char* name)
{
    if (condition) return;
    ++failures;
    std::cerr << "FAIL session_sync: " << name << '\n';
}
}

int main()
{
    session_sync::Tracker sync;
    check(sync.baseline(), "cold boot starts as baseline");
    sync.note(session_sync::Method::ThreadStatus);
    sync.note(session_sync::Method::ThreadStatus);
    check(sync.baseline(), "intermediate thread snapshots stay baseline");
    sync.note(session_sync::Method::DeviceStatus);
    check(sync.baseline(), "partial handshake stays baseline");
    const uint32_t handshake_at = 4000;
    sync.note(session_sync::Method::LightingConfig, handshake_at);
    check(sync.baseline(handshake_at), "completing handshake is still restore");
    check(sync.baseline(handshake_at + session_sync::kHandshakeSettleMs - 1),
          "follow-up lamps inside the settle window stay restore");
    check(!sync.baseline(handshake_at + session_sync::kHandshakeSettleMs),
          "after settle, live events");
    sync.begin();
    check(sync.baseline(), "real session loss starts a new baseline");
    if (failures) return EXIT_FAILURE;
    std::cout << "PASS session_sync (7 scenarios)\n";
    return EXIT_SUCCESS;
}
