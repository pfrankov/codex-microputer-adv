#include <cassert>
#include <cstdio>
#include <limits>
#include <initializer_list>

#include "key_layout.h"

int main()
{
    // [ and ] are context-free encoder detents; the click is on Enter, and
    // backslash is unbound. Codex, not the device, decides whether a detent
    // means navigation or reasoning.
    assert(keys::map_layout(1, 11).key == keys::Key::EncoderLeft);
    assert(keys::map_layout(1, 12).key == keys::Key::EncoderRight);
    assert(keys::map_layout(1, 13).key == keys::Key::Other);
    assert(keys::needs_release(keys::Key::Enter));
    assert(keys::map_layout(3, 12).key == keys::Key::Right);

    // Preserve the remaining navigation controls around the punctuation row.
    assert(keys::map_layout(2, 11).key == keys::Key::Up);
    assert(keys::map_layout(3, 10).key == keys::Key::Left);
    assert(keys::map_layout(3, 11).key == keys::Key::Down);
    assert(keys::map_layout(3, 12).key == keys::Key::Right);
    assert(keys::map_layout(3, 13).key == keys::Key::Enter);

    // T/Y/U/I/O/P expose every host-configurable Micro command slot before
    // ACT12 on Enter. Their meaning remains owned by Codex settings.
    for (int col = 5; col <= 10; ++col) {
        const auto action = keys::map_layout(1, col);
        assert(action.key == keys::Key::NativeAction);
        assert(action.digit == col + 1);
        assert(keys::needs_release(action.key));
    }
    assert(keys::needs_release(keys::Key::Digit));
    const auto combined_voice = keys::map_layout(2, 2);
    assert(combined_voice.key == keys::Key::NativeAction);
    assert(combined_voice.digit == 1011);

    // Every native command, including the compound A action, has a distinct
    // in-bounds held-transport slot for its matching physical release.
    bool used[keys::kNativeActionCount] = {};
    for (int slot : {6, 7, 8, 9, 10, 11, 12, combined_voice.digit}) {
        const int index = keys::native_action_index(slot);
        assert(index >= 0 && index < keys::kNativeActionCount);
        assert(!used[index]);
        used[index] = true;
    }
    for (int slot : {std::numeric_limits<int>::min(), -1, 0, 5, 13, 1010, 1012,
                     std::numeric_limits<int>::max()}) {
        assert(keys::native_action_index(slot) == -1);
    }
    assert(keys::needs_release(combined_voice.key));

    std::puts("PASS key_layout (19 bindings, native action release indices)");
}
