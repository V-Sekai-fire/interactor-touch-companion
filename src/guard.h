// SPDX-License-Identifier: Apache-2.0 OR MIT
#pragma once
#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "keymap.h"

namespace companion {

// Keys and clicks go out only while the foreground window belongs to the game process the companion was given.
class Guard {
public:
    explicit Guard(DWORD gamePid, std::function<HWND()> foreground = ::GetForegroundWindow)
        : pid_(gamePid), foreground_(std::move(foreground)) {}
    bool allows(std::string* why) const;
    DWORD pid() const { return pid_; }

private:
    DWORD pid_;
    std::function<HWND()> foreground_;
};

class Sender {
public:
    using SendInputFn = std::function<UINT(UINT, INPUT*, int)>;
    using PostFn = std::function<BOOL(HWND, UINT, WPARAM, LPARAM)>;
    explicit Sender(Guard guard, SendInputFn send = ::SendInput, PostFn post = ::PostMessageW)
        : guard_(std::move(guard)), send_(std::move(send)), post_(std::move(post)) {}
    bool key(const Key& k, bool down, std::string* why);
    // A click on the game's client area, posted to its window so the real cursor stays where the agent put it.
    bool click(HWND game, int x, int y, bool down, std::string* why);
    // Lets go of held keys, only while the game still has the foreground.
    void releaseAll();
    const Guard& guard() const { return guard_; }
    std::uint64_t refused() const { return refused_; }

private:
    Guard guard_;
    SendInputFn send_;
    PostFn post_;
    std::set<const Key*> held_;
    std::uint64_t refused_ = 0;
};

INPUT keyInput(const Key& k, bool down);
// The game's largest visible top-level window, or nullptr.
HWND mainWindowOf(DWORD pid);

}
