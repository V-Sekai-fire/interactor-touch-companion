// SPDX-License-Identifier: Apache-2.0 OR MIT
#include "guard.h"

namespace companion {

bool Guard::allows(std::string* why) const {
    HWND fg = foreground_();
    DWORD owner = 0;
    if (fg) GetWindowThreadProcessId(fg, &owner);
    if (fg && owner == pid_) return true;
    if (why) *why = "guard: the foreground window belongs to pid " + std::to_string(owner) + ", not the game (pid " +
                    std::to_string(pid_) + ")";
    return false;
}

INPUT keyInput(const Key& k, bool down) {
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = k.vk;
    in.ki.wScan = k.scan;
    in.ki.dwFlags = KEYEVENTF_SCANCODE | (k.extended ? KEYEVENTF_EXTENDEDKEY : 0) | (down ? 0 : KEYEVENTF_KEYUP);
    return in;
}

bool Sender::key(const Key& k, bool down, std::string* why) {
    if (!guard_.allows(why)) {
        ++refused_;
        return false;
    }
    INPUT in = keyInput(k, down);
    if (send_(1, &in, sizeof(INPUT)) != 1) {
        if (why) *why = "SendInput delivered nothing (blocked by UIPI or a secure desktop)";
        return false;
    }
    if (down) held_.insert(&k);
    else held_.erase(&k);
    return true;
}

bool Sender::click(HWND game, int x, int y, bool down, std::string* why) {
    if (!guard_.allows(why)) {
        ++refused_;
        return false;
    }
    LPARAM at = MAKELPARAM(x, y);
    if (down) post_(game, WM_MOUSEMOVE, 0, at);
    return post_(game, down ? WM_LBUTTONDOWN : WM_LBUTTONUP, down ? MK_LBUTTON : 0, at) != FALSE;
}

void Sender::releaseAll() {
    std::set<const Key*> held = held_;
    for (const Key* k : held) key(*k, false, nullptr);
}

HWND mainWindowOf(DWORD pid) {
    struct Search {
        DWORD pid;
        HWND best = nullptr;
        long area = 0;
    } s{pid};
    EnumWindows(
        [](HWND w, LPARAM p) -> BOOL {
            Search& s = *reinterpret_cast<Search*>(p);
            DWORD owner = 0;
            GetWindowThreadProcessId(w, &owner);
            RECT r{};
            if (owner != s.pid || !IsWindowVisible(w) || GetWindow(w, GW_OWNER) || !GetWindowRect(w, &r)) return TRUE;
            long area = (r.right - r.left) * (r.bottom - r.top);
            if (area > s.area) s = Search{s.pid, w, area};
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&s));
    return s.best;
}

}
