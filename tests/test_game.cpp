// SPDX-License-Identifier: Apache-2.0 OR MIT
// A stand-in game for the tests: a window of one colour that logs the keys and clicks it receives.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cwchar>

namespace {
COLORREF color = RGB(255, 0, 255);
FILE* events = nullptr;

LRESULT CALLBACK proc(HWND w, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_KEYDOWN:
    case WM_KEYUP:
        if (events) std::fprintf(events, "%s vk=%u scan=%u ext=%u\n", m == WM_KEYDOWN ? "keydown" : "keyup", unsigned(wp),
                              unsigned((lp >> 16) & 0xFF), unsigned((lp >> 24) & 1));
        break;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
        if (events) std::fprintf(events, "%s x=%d y=%d\n", m == WM_LBUTTONDOWN ? "mousedown" : "mouseup", int(short(LOWORD(lp))), int(short(HIWORD(lp))));
        break;
    case WM_ERASEBKGND: {
        RECT r;
        GetClientRect(w, &r);
        HBRUSH b = CreateSolidBrush(color);
        FillRect(reinterpret_cast<HDC>(wp), &r, b);
        DeleteObject(b);
        return 1;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    if (events) std::fflush(events);
    return DefWindowProcW(w, m, wp, lp);
}
}

int wmain(int argc, wchar_t** argv) {
    int x = 100, y = 100, width = 640, height = 480, ms = 0;
    bool activate = true, ghost = false;
    for (int i = 1; i < argc; ++i) {
        bool more = i + 1 < argc;
        if (!std::wcscmp(argv[i], L"--no-activate")) activate = false;
        else if (!std::wcscmp(argv[i], L"--ghost")) ghost = true;
        else if (more && !std::wcscmp(argv[i], L"--color")) color = std::wcstoul(argv[++i], nullptr, 16), color = RGB(color >> 16, (color >> 8) & 0xFF, color & 0xFF);
        else if (more && !std::wcscmp(argv[i], L"--x")) x = _wtoi(argv[++i]);
        else if (more && !std::wcscmp(argv[i], L"--y")) y = _wtoi(argv[++i]);
        else if (more && !std::wcscmp(argv[i], L"--w")) width = _wtoi(argv[++i]);
        else if (more && !std::wcscmp(argv[i], L"--h")) height = _wtoi(argv[++i]);
        else if (more && !std::wcscmp(argv[i], L"--ms")) ms = _wtoi(argv[++i]);
        else if (more && !std::wcscmp(argv[i], L"--log")) events = _wfopen(argv[++i], L"w");
    }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WNDCLASSW wc{};
    wc.lpfnWndProc = proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"companion-test-game";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc);
    HWND w = CreateWindowExW(activate ? 0 : WS_EX_NOACTIVATE, wc.lpszClassName, L"companion test game", WS_OVERLAPPEDWINDOW, x, y, width,
                             height, nullptr, nullptr, wc.hInstance, nullptr);
    if (ghost) {
        SetWindowLongPtrW(w, GWL_EXSTYLE, GetWindowLongPtrW(w, GWL_EXSTYLE) | WS_EX_LAYERED | WS_EX_TRANSPARENT);
        SetLayeredWindowAttributes(w, 0, 1, LWA_ALPHA);
    }
    ShowWindow(w, activate ? SW_SHOW : SW_SHOWNOACTIVATE);
    if (ms > 0) SetTimer(w, 1, ms, [](HWND h, UINT, UINT_PTR, DWORD) { DestroyWindow(h); });
    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    if (events) std::fclose(events);
    return 0;
}
