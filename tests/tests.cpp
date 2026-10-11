// SPDX-License-Identifier: Apache-2.0 OR MIT
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "capture.h"
#include "guard.h"
#include "keymap.h"

using namespace companion;

namespace {

int failures = 0;
#define CHECK(expr)                                                                \
    do {                                                                           \
        if (!(expr)) {                                                             \
            ++failures;                                                            \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr);   \
        }                                                                          \
    } while (0)

std::string root, testGame, companionExe;

KeyMap defaultMap() {
    std::string map, table, error;
    readFile(root + "/keymaps/default.json", map);
    readFile(root + "/keymaps/keys.json", table);
    KeyMap m;
    loadKeyMap(map, table, m, &error);
    return m;
}

const Key* find(const KeyMap& m, const std::string& code) {
    for (const Key& k : m.keys)
        if (k.code == code) return &k;
    return nullptr;
}

HWND ownWindow(int x, int y, int w, int h, COLORREF* color) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = [](HWND hw, UINT m, WPARAM wp, LPARAM lp) -> LRESULT {
        if (m == WM_ERASEBKGND) {
            RECT r;
            GetClientRect(hw, &r);
            HBRUSH b = CreateSolidBrush(*reinterpret_cast<COLORREF*>(GetWindowLongPtrW(hw, GWLP_USERDATA)));
            FillRect(reinterpret_cast<HDC>(wp), &r, b);
            DeleteObject(b);
            return 1;
        }
        return DefWindowProcW(hw, m, wp, lp);
    };
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"companion-tests";
    RegisterClassW(&wc);
    HWND hw = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, wc.lpszClassName, L"companion tests", WS_POPUP, x, y, w, h, nullptr,
                              nullptr, wc.hInstance, nullptr);
    SetWindowLongPtrW(hw, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(color));
    return hw;
}

void pump(int ms) {
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end) {
        MSG m;
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&m);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

// A window far left of every monitor, for tests that need a window but not its pixels.
int offscreenX() { return GetSystemMetrics(SM_XVIRTUALSCREEN) - 4000; }

// Capture sees black for windows off every monitor, so the GPU tests use on-screen windows at alpha 1/255
// that clicks pass through: captured in full colour, invisible to the person at the desk.
void ghost(HWND w) {
    SetWindowLongPtrW(w, GWL_EXSTYLE, GetWindowLongPtrW(w, GWL_EXSTYLE) | WS_EX_LAYERED | WS_EX_TRANSPARENT);
    SetLayeredWindowAttributes(w, 0, 1, LWA_ALPHA);
}

void keymapDefault() {
    KeyMap m = defaultMap();
    CHECK(m.name == "default");
    CHECK(m.keys.size() == 6);
    const Key* up = find(m, "ArrowUp");
    CHECK(up && up->vk == VK_UP && up->scan == 0x48 && up->extended);
    const Key* space = find(m, "Space");
    CHECK(space && space->vk == VK_SPACE && space->scan == 0x39 && !space->extended && space->span == 2);
}

void keymapUnknown() {
    std::string table, error;
    readFile(root + "/keymaps/keys.json", table);
    KeyMap m;
    CHECK(!loadKeyMap(R"({"keys": [{"code": "AltLeft"}]})", table, m, &error));
    CHECK(error.find("AltLeft") != std::string::npos);
    CHECK(loadKeyMap(R"({"keys": [{"code": "KeyZ"}]})", table, m, &error));
}

void layoutMatchesBrowser() {
    KeyMap m = defaultMap();
    std::vector<Button> b = layoutButtons(m, 48.0f, 4.0f);
    auto at = [&](const char* code) {
        for (const Button& x : b)
            if (x.key->code == code) return x;
        return Button{};
    };
    CHECK(at("ArrowUp").w == 48.0f);
    CHECK(at("Space").w == 100.0f);
    CHECK(at("ArrowDown").x - at("ArrowLeft").x == 52.0f);
    CHECK(at("ArrowDown").contains(at("ArrowDown").x + 1, at("ArrowDown").y + 1));
    CHECK(!at("ArrowDown").contains(at("ArrowDown").x - 2, at("ArrowDown").y + 1));
}

void guardRefusesForeign() {
    COLORREF c = RGB(0, 0, 0);
    HWND mine = ownWindow(offscreenX(), 0, 64, 64, &c);
    HWND shell = GetShellWindow();
    std::string why;
    Guard refuses(GetCurrentProcessId(), [&] { return shell; });
    CHECK(!refuses.allows(&why));
    CHECK(why.find("guard") != std::string::npos);
    Guard none(GetCurrentProcessId(), [] { return HWND(nullptr); });
    CHECK(!none.allows(nullptr));
    Guard allows(GetCurrentProcessId(), [&] { return mine; });
    CHECK(allows.allows(nullptr));
    DestroyWindow(mine);
}

void guardRefusesLiveForeground() {
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    wchar_t cmd[] = L"cmd.exe /c ping -n 3 127.0.0.1 >nul";
    CHECK(CreateProcessW(nullptr, cmd, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi));
    std::vector<INPUT> sent;
    Sender s(Guard(pi.dwProcessId), [&](UINT n, INPUT* in, int) { sent.insert(sent.end(), in, in + n); return n; });
    KeyMap m = defaultMap();
    std::string why;
    CHECK(!s.key(*find(m, "ArrowLeft"), true, &why));
    CHECK(sent.empty() && s.refused() == 1);
    TerminateProcess(pi.hProcess, 0);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
}

void senderExactKey() {
    COLORREF c = RGB(0, 0, 0);
    HWND game = ownWindow(offscreenX(), 0, 64, 64, &c);
    std::vector<INPUT> sent;
    Sender s(Guard(GetCurrentProcessId(), [&] { return game; }), [&](UINT n, INPUT* in, int size) {
        CHECK(size == int(sizeof(INPUT)));
        sent.insert(sent.end(), in, in + n);
        return n;
    });
    KeyMap m = defaultMap();
    for (const Key& k : m.keys) {
        sent.clear();
        CHECK(s.key(k, true, nullptr));
        CHECK(s.key(k, false, nullptr));
        CHECK(sent.size() == 2);
        if (sent.size() != 2) continue;
        DWORD base = KEYEVENTF_SCANCODE | (k.extended ? KEYEVENTF_EXTENDEDKEY : 0);
        CHECK(sent[0].type == INPUT_KEYBOARD && sent[0].ki.wScan == k.scan && sent[0].ki.wVk == k.vk && sent[0].ki.dwFlags == base);
        CHECK(sent[1].ki.wScan == k.scan && sent[1].ki.dwFlags == (base | KEYEVENTF_KEYUP));
    }
    DestroyWindow(game);
}

void senderRefusedSendsNothing() {
    std::vector<INPUT> sent;
    std::vector<UINT> posted;
    HWND shell = GetShellWindow();
    Sender s(
        Guard(GetCurrentProcessId(), [&] { return shell; }), [&](UINT n, INPUT* in, int) { sent.insert(sent.end(), in, in + n); return n; },
        [&](HWND, UINT msg, WPARAM, LPARAM) { posted.push_back(msg); return TRUE; });
    KeyMap m = defaultMap();
    for (const Key& k : m.keys) CHECK(!s.key(k, true, nullptr));
    CHECK(!s.click(shell, 10, 10, true, nullptr));
    CHECK(sent.empty() && posted.empty());
    CHECK(s.refused() == m.keys.size() + 1);
}

void senderClickPosts() {
    COLORREF c = RGB(0, 0, 0);
    HWND game = ownWindow(offscreenX(), 0, 64, 64, &c);
    std::vector<std::pair<UINT, LPARAM>> posted;
    Sender s(Guard(GetCurrentProcessId(), [&] { return game; }), [](UINT n, INPUT*, int) { return n; },
             [&](HWND to, UINT msg, WPARAM, LPARAM lp) {
                 CHECK(to == game);
                 posted.push_back({msg, lp});
                 return TRUE;
             });
    CHECK(s.click(game, 30, 40, true, nullptr));
    CHECK(s.click(game, 30, 40, false, nullptr));
    CHECK(posted.size() == 3);
    if (posted.size() == 3) {
        CHECK(posted[0].first == WM_MOUSEMOVE && posted[1].first == WM_LBUTTONDOWN && posted[2].first == WM_LBUTTONUP);
        CHECK(posted[1].second == MAKELPARAM(30, 40));
    }
    DestroyWindow(game);
}

COLORREF centre(ID3D11Device* device, ID3D11Texture2D* t, UINT w, UINT h) {
    D3D11_TEXTURE2D_DESC d{};
    t->GetDesc(&d);
    d.Usage = D3D11_USAGE_STAGING;
    d.BindFlags = 0;
    d.MiscFlags = 0;
    d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    winrt::com_ptr<ID3D11Texture2D> staging;
    device->CreateTexture2D(&d, nullptr, staging.put());
    winrt::com_ptr<ID3D11DeviceContext> ctx;
    device->GetImmediateContext(ctx.put());
    ctx->CopyResource(staging.get(), t);
    D3D11_MAPPED_SUBRESOURCE map{};
    ctx->Map(staging.get(), 0, D3D11_MAP_READ, 0, &map);
    const std::uint8_t* p = static_cast<const std::uint8_t*>(map.pData) + (h / 2) * map.RowPitch + (w / 2) * 4;
    COLORREF c = RGB(p[2], p[1], p[0]);
    ctx->Unmap(staging.get(), 0);
    return c;
}

void captureFrames() {
    winrt::com_ptr<ID3D11Device> device;
    CHECK(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                      D3D11_SDK_VERSION, device.put(), nullptr, nullptr)));
    if (!device) return;
    COLORREF color = RGB(255, 0, 255);
    HWND w = ownWindow(GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN), 320, 240, &color);
    ghost(w);
    ShowWindow(w, SW_SHOWNOACTIVATE);
    UpdateWindow(w);
    Capture cap;
    std::string error;
    CHECK(cap.start(w, device.get(), nullptr, &error));
    if (!error.empty()) std::fprintf(stderr, "%s\n", error.c_str());
    auto seen = [&](COLORREF want) {
        COLORREF got = 0;
        for (int i = 0; i < 300 && got != want; ++i) {
            pump(10);
            cap.next([&](ID3D11Texture2D* t, UINT fw, UINT fh) { got = centre(device.get(), t, fw, fh); });
        }
        if (got != want) std::fprintf(stderr, "centre pixel %06lx, wanted %06lx\n", got, want);
        return got == want;
    };
    CHECK(seen(RGB(255, 0, 255)));
    CHECK(cap.frames() > 0);
    color = RGB(0, 255, 0);
    InvalidateRect(w, nullptr, TRUE);
    CHECK(seen(RGB(0, 255, 0)));
    cap.stop();
    DestroyWindow(w);
}

std::string quote(const std::string& s) { return "\"" + s + "\""; }

void companionPresents() {
    std::string tmp = std::filesystem::temp_directory_path().string();
    std::string state = tmp + "/companion-state.json", bmp = tmp + "/companion-frame.bmp";
    std::remove(state.c_str());
    std::remove(bmp.c_str());
    std::string game = quote(testGame) + " --no-activate --ghost --color 00FF00 --ms 15000 --w 640 --h 480 --x " +
                       std::to_string(GetSystemMetrics(SM_XVIRTUALSCREEN)) + " --y " + std::to_string(GetSystemMetrics(SM_YVIRTUALSCREEN));
    STARTUPINFOA si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    CHECK(CreateProcessA(nullptr, game.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi));
    std::string cmd = quote(companionExe) + " --map " + quote(root + "/keymaps/default.json") + " --keys " + quote(root + "/keymaps/keys.json") +
                      " --ghost --size 64 --gap 8 --frames 120 --pid " + std::to_string(pi.dwProcessId) + " --state " + quote(state) +
                      " --capture-bmp " + quote(bmp);
    STARTUPINFOA si2{sizeof(si2)};
    PROCESS_INFORMATION pc{};
    CHECK(CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si2, &pc));
    CHECK(WaitForSingleObject(pc.hProcess, 30000) == WAIT_OBJECT_0);
    DWORD code = 99;
    GetExitCodeProcess(pc.hProcess, &code);
    CHECK(code == 0);
    TerminateProcess(pi.hProcess, 0);
    for (HANDLE h : {pi.hProcess, pi.hThread, pc.hProcess, pc.hThread}) CloseHandle(h);
    std::ifstream in(state);
    nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
    CHECK(!j.is_discarded());
    if (j.is_discarded()) return;
    CHECK(j["captured"].get<int>() > 0);
    CHECK(j["buttons"].size() == 6);
    CHECK(j["picture"]["w"].get<int>() > 0);
    CHECK(j["sent"].empty() && j["refused"].get<int>() == 0);
    std::ifstream f(bmp, std::ios::binary);
    std::vector<char> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    CHECK(data.size() > 54);
    if (data.size() <= 54) return;
    std::int32_t off, w, h;
    std::uint16_t bpp;
    std::memcpy(&off, &data[10], 4);
    std::memcpy(&w, &data[18], 4);
    std::memcpy(&h, &data[22], 4);
    std::memcpy(&bpp, &data[28], 2);
    int stride = ((w * bpp / 8) + 3) & ~3;
    int x = w / 2, yTop = std::abs(h) / 5;
    int row = h > 0 ? h - 1 - yTop : yTop;
    const unsigned char* p = reinterpret_cast<const unsigned char*>(&data[off + row * stride + x * (bpp / 8)]);
    std::fprintf(stderr, "companion frame %dx%d centre-top pixel b=%u g=%u r=%u\n", w, h, p[0], p[1], p[2]);
    CHECK(p[1] > 200 && p[0] < 60 && p[2] < 60);
}

}

int wmain(int argc, wchar_t** argv) {
    std::map<std::string, std::function<void()>> cases = {
        {"keymap.default-map-loads", keymapDefault},
        {"keymap.unknown-code-refused", keymapUnknown},
        {"layout.matches-browser-keypad", layoutMatchesBrowser},
        {"guard.refuses-foreign-foreground", guardRefusesForeign},
        {"guard.refuses-live-foreground-for-windowless-game", guardRefusesLiveForeground},
        {"sender.button-sends-exactly-its-key", senderExactKey},
        {"sender.refused-sends-nothing", senderRefusedSendsNothing},
        {"sender.click-posts-to-the-game", senderClickPosts},
        {"gpu.capture-yields-frames-from-a-test-window", captureFrames},
        {"gpu.companion-presents-the-captured-game", companionPresents},
    };
    if (argc < 2) {
        for (auto& c : cases) std::printf("%s\n", c.first.c_str());
        return 0;
    }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    std::string name = winrt::to_string(argv[1]);
    if (argc > 2) root = winrt::to_string(argv[2]);
    if (argc > 3) testGame = winrt::to_string(argv[3]);
    if (argc > 4) companionExe = winrt::to_string(argv[4]);
    auto it = cases.find(name);
    if (it == cases.end()) {
        std::fprintf(stderr, "unknown case %s\n", name.c_str());
        return 2;
    }
    it->second();
    return failures ? 1 : 0;
}
