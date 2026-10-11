// SPDX-License-Identifier: Apache-2.0 OR MIT
// touch-companion: shows a game's window and large key buttons in one window over the game, so an agent
// that can only click can play a keyboard game. Clicks never take focus; keys reach the game only while it
// is the foreground window.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <dwmapi.h>
#include <nlohmann/json.hpp>

#include "capture.h"
#include "guard.h"
#include "keymap.h"
#include "panelspun/window.h"
#include "frame_share.h"

using namespace companion;

namespace {

struct Options {
    std::string map = "keymaps/default.json";
    std::string keys = "keymaps/keys.json";
    std::string state;
    std::string captureBmp;
    DWORD pid = 0;
    std::wstring launch;
    float size = 96.0f;
    float gap = 16.0f;
    float viewScale = 1.0f;
    int frames = 0;
    int waitMs = 15000;
    bool ghost = false;
};

struct Shared {
    Options opt;
    KeyMap map;
    HWND game = nullptr;
    HANDLE process = nullptr;
    std::unique_ptr<Sender> sender;
    panelspun::Window* window = nullptr;
    RECT gameRect{};
    int keysHeight = 0;
    panelspun::Rect picture{};
    std::vector<Button> buttons;
    float buttonsX = 0.0f;
    float buttonsY = 0.0f;
    std::uint64_t captured = 0;
    std::uint64_t presented = 0;
    std::deque<nlohmann::json> sent;
    std::string status = "ready";
    bool dirty = true;

    void note(const std::string& code, bool down, bool ok, const std::string& why) {
        sent.push_back({{"code", code}, {"down", down}, {"sent", ok}});
        if (sent.size() > 64) sent.pop_front();
        status = ok ? (down ? code : "ready") : why;
        dirty = true;
    }

    POINT origin() const {
        POINT p{0, 0};
        ClientToScreen(static_cast<HWND>(window->nativeHandle()), &p);
        return p;
    }

    void writeState() {
        if (opt.state.empty() || !dirty) return;
        dirty = false;
        POINT o = origin();
        nlohmann::json j;
        j["pid"] = opt.pid;
        j["map"] = map.name;
        j["picture"] = {{"x", o.x + picture.x}, {"y", o.y + picture.y}, {"w", picture.w}, {"h", picture.h}};
        j["buttons"] = nlohmann::json::array();
        for (const Button& b : buttons)
            j["buttons"].push_back({{"code", b.key->code}, {"x", o.x + buttonsX + b.x + b.w / 2}, {"y", o.y + buttonsY + b.y + b.h / 2},
                                    {"w", b.w}, {"h", b.h}});
        j["captured"] = captured;
        j["presented"] = presented;
        j["refused"] = sender->refused();
        j["sent"] = nlohmann::json(sent);
        j["status"] = status;
        std::string tmp = opt.state + ".tmp";
        std::ofstream(tmp, std::ios::trunc) << j.dump(1);
        MoveFileExA(tmp.c_str(), opt.state.c_str(), MOVEFILE_REPLACE_EXISTING);
    }
};

RECT frameBounds(HWND w) {
    RECT r{};
    if (FAILED(DwmGetWindowAttribute(w, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r)))) GetWindowRect(w, &r);
    return r;
}

class GamePanel : public panelspun::Panel {
public:
    explicit GamePanel(Shared& s) : Panel("game"), s_(s) {}
    void draw(panelspun::DrawContext&) override {}
    bool usesVulkanRegion() const override { return true; }

    void update(const panelspun::VulkanContext& vk) override {
        if (!started_) {
            started_ = true;
            std::string error;
            if (!share_.init(vk, &error) ||
                !capture_.start(s_.game, share_.device(), [w = s_.window] { w->requestRedrawFromAnyThread(); }, &error)) {
                std::fprintf(stderr, "touch-companion: %s\n", error.c_str());
                s_.status = error;
                s_.dirty = true;
            }
        }
        capture_.next([&](ID3D11Texture2D* t, UINT w, UINT h) {
            std::string error;
            if (!share_.copy(t, w, h, &error)) std::fprintf(stderr, "touch-companion: %s\n", error.c_str());
        });
        s_.captured = capture_.frames();
        if (s_.process && WaitForSingleObject(s_.process, 0) == WAIT_OBJECT_0) s_.window->requestClose();
        RECT r = frameBounds(s_.game);
        if (!EqualRect(&r, &s_.gameRect)) {
            s_.gameRect = r;
            s_.window->setBounds(r.left, r.top, r.right - r.left, r.bottom - r.top);
        }
        if (!s_.opt.captureBmp.empty() && share_.ready()) s_.window->captureNextFrame(s_.opt.captureBmp);
        s_.writeState();
    }

    void recordVulkan(const panelspun::VulkanRegionFrame& f) override {
        VkRect2D dst{};
        if (share_.ready()) {
            float fit = std::min(f.width / float(share_.width()), f.height / float(share_.height())) * s_.opt.viewScale;
            dst.extent = {std::uint32_t(share_.width() * fit), std::uint32_t(share_.height() * fit)};
            dst.offset = {int32_t((f.width - dst.extent.width) / 2), int32_t((f.height - dst.extent.height) / 2)};
        }
        panelspun::Rect pic{dst.offset.x, dst.offset.y, int(dst.extent.width), int(dst.extent.height)};
        if (!(pic == s_.picture)) {
            s_.picture = pic;
            s_.dirty = true;
        }
        share_.record(f, dst);
        ++s_.presented;
        if (s_.presented % 30 == 1) s_.dirty = true;
    }

    bool pointer(const panelspun::PointerEvent& e) override {
        if ((e.action != panelspun::PointerAction::Down && e.action != panelspun::PointerAction::Up) || !share_.ready()) return false;
        const panelspun::Rect& p = s_.picture;
        if (!p.contains(int(e.x), int(e.y)) && e.action == panelspun::PointerAction::Down) return false;
        RECT frame = frameBounds(s_.game);
        POINT client{0, 0};
        ClientToScreen(s_.game, &client);
        int x = int((e.x - p.x) * share_.width() / std::max(1, p.w)) - (client.x - frame.left);
        int y = int((e.y - p.y) * share_.height() / std::max(1, p.h)) - (client.y - frame.top);
        bool down = e.action == panelspun::PointerAction::Down;
        std::string why;
        bool ok = s_.sender->click(s_.game, x, y, down, &why);
        s_.note("Mouse(" + std::to_string(x) + "," + std::to_string(y) + ")", down, ok, why);
        return false;
    }

private:
    Shared& s_;
    FrameShare share_;
    Capture capture_;
    bool started_ = false;
};

class KeysPanel : public panelspun::Panel {
public:
    explicit KeysPanel(Shared& s) : Panel("keys"), s_(s) {}

    void draw(panelspun::DrawContext& ctx) override {
        float w = 0.0f, h = 0.0f;
        for (const Button& b : s_.buttons) {
            w = std::max(w, b.x + b.w);
            h = std::max(h, b.y + b.h);
        }
        float top = std::floor((ctx.height - h) / 2);
        s_.buttonsX = std::floor((ctx.width - w) / 2);
        s_.buttonsY = top + float(windowHeight() - ctx.height);
        for (const Button& b : s_.buttons) {
            bool held = std::find(down_.begin(), down_.end(), &b) != down_.end();
            panelspun::drawRect(ctx, s_.buttonsX + b.x, top + b.y, b.w, b.h, 12.0f, held ? panelspun::Color{192, 48, 128} : panelspun::Color{32, 48, 64});
            panelspun::drawText(ctx, b.key->label, s_.opt.size / 3, s_.buttonsX + b.x + b.w / 2, top + b.y + b.h / 2 - s_.opt.size / 6, 0.5f,
                                panelspun::Color{255, 255, 255});
        }
        panelspun::drawText(ctx, s_.status, 14.0f, 8.0f, 4.0f, 0.0f, panelspun::Color{200, 200, 200});
        top_ = top;
    }

    bool pointer(const panelspun::PointerEvent& e) override {
        if (e.action == panelspun::PointerAction::Down) {
            for (const Button& b : s_.buttons) {
                if (!b.contains(e.x - s_.buttonsX, e.y - top_)) continue;
                std::string why;
                bool ok = s_.sender->key(*b.key, true, &why);
                s_.note(b.key->code, true, ok, why);
                if (ok) {
                    held_[e.pointerId] = &b;
                    down_.push_back(&b);
                }
                return true;
            }
        } else if (e.action == panelspun::PointerAction::Up || e.action == panelspun::PointerAction::Leave) {
            auto it = held_.find(e.pointerId);
            if (it == held_.end()) return false;
            const Button* b = it->second;
            held_.erase(it);
            down_.erase(std::remove(down_.begin(), down_.end(), b), down_.end());
            std::string why;
            bool ok = s_.sender->key(*b->key, false, &why);
            s_.note(b->key->code, false, ok, why);
            return true;
        }
        return false;
    }

    void focusLost() override {}

private:
    int windowHeight() const {
        RECT r{};
        GetClientRect(static_cast<HWND>(s_.window->nativeHandle()), &r);
        return r.bottom;
    }
    Shared& s_;
    std::map<std::uint64_t, const Button*> held_;
    std::vector<const Button*> down_;
    float top_ = 0.0f;
};

bool parse(int argc, wchar_t** argv, Options& o) {
    auto narrow = [](const wchar_t* w) { return winrt::to_string(w); };
    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? narrow(argv[++i]) : std::string(); };
        if (a == L"--map") o.map = next();
        else if (a == L"--keys") o.keys = next();
        else if (a == L"--state") o.state = next();
        else if (a == L"--capture-bmp") o.captureBmp = next();
        else if (a == L"--pid") o.pid = std::stoul(next());
        else if (a == L"--size") o.size = std::stof(next());
        else if (a == L"--gap") o.gap = std::stof(next());
        else if (a == L"--view-scale") o.viewScale = std::stof(next());
        else if (a == L"--frames") o.frames = std::stoi(next());
        else if (a == L"--wait-ms") o.waitMs = std::stoi(next());
        else if (a == L"--ghost") o.ghost = true;
        else if (a == L"--") {
            for (++i; i < argc; ++i) o.launch += (o.launch.empty() ? L"\"" : L" \"") + std::wstring(argv[i]) + L"\"";
        } else {
            std::fprintf(stderr, "touch-companion: unknown argument %s\n", narrow(argv[i]).c_str());
            return false;
        }
    }
    if (!o.pid && o.launch.empty()) {
        std::fprintf(stderr,
                     "usage: touch-companion [--map m.json] [--keys keys.json] [--size px] [--gap px] [--view-scale s]\n"
                     "                       [--state out.json] [--frames n] (--pid N | -- game.exe args...)\n");
        return false;
    }
    return true;
}

}

int wmain(int argc, wchar_t** argv) {
    Shared s;
    if (!parse(argc, argv, s.opt)) return 2;
    std::string mapText, tableText, error;
    if (!readFile(s.opt.map, mapText) || !readFile(s.opt.keys, tableText) || !loadKeyMap(mapText, tableText, s.map, &error)) {
        std::fprintf(stderr, "touch-companion: cannot load %s with %s %s\n", s.opt.map.c_str(), s.opt.keys.c_str(), error.c_str());
        return 2;
    }
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
    } catch (const winrt::hresult_error&) {
    }
    if (!s.opt.launch.empty()) {
        STARTUPINFOW si{sizeof(si)};
        PROCESS_INFORMATION pi{};
        std::wstring cmd = s.opt.launch;
        if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
            std::fprintf(stderr, "touch-companion: could not start the game (error %lu)\n", GetLastError());
            return 3;
        }
        CloseHandle(pi.hThread);
        s.process = pi.hProcess;
        s.opt.pid = pi.dwProcessId;
    } else {
        s.process = OpenProcess(SYNCHRONIZE, FALSE, s.opt.pid);
    }
    for (int waited = 0; !(s.game = mainWindowOf(s.opt.pid)) && waited < s.opt.waitMs; waited += 100) Sleep(100);
    if (!s.game) {
        std::fprintf(stderr, "touch-companion: pid %lu shows no window\n", s.opt.pid);
        return 3;
    }
    s.sender = std::make_unique<Sender>(Guard(s.opt.pid));
    s.buttons = layoutButtons(s.map, s.opt.size, s.opt.gap);
    float rows = 0.0f;
    for (const Button& b : s.buttons) rows = std::max(rows, b.y + b.h);
    s.keysHeight = int(rows + 2 * s.opt.gap + 24.0f);
    s.gameRect = frameBounds(s.game);

    panelspun::WindowConfig config;
    config.title = "touch-companion";
    config.width = s.gameRect.right - s.gameRect.left;
    config.height = s.gameRect.bottom - s.gameRect.top;
    config.panelHeaders = false;
    config.lockLayout = true;
    config.notFocusable = true;
    config.alwaysOnTop = true;
    config.borderless = true;
    config.hidden = true;
    config.tickHz = 10;
    config.deviceExtensions = {VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME};
    panelspun::SplitTree layout("game");
    layout.dock("keys", "game", panelspun::Side::Bottom, std::clamp(double(s.keysHeight) / std::max(1, config.height), 0.05, 0.95));
    std::unique_ptr<panelspun::Window> window = panelspun::Window::create(config, layout, &error);
    if (!window) {
        std::fprintf(stderr, "touch-companion: %s\n", error.c_str());
        return 4;
    }
    s.window = window.get();
    window->setBounds(s.gameRect.left, s.gameRect.top, config.width, config.height);
    if (s.opt.ghost) {
        // Alpha 1/255 and click-through: the tests see its frames while the desk sees nothing.
        HWND hw = static_cast<HWND>(window->nativeHandle());
        SetWindowLongPtrW(hw, GWL_EXSTYLE, GetWindowLongPtrW(hw, GWL_EXSTYLE) | WS_EX_LAYERED | WS_EX_TRANSPARENT);
        SetLayeredWindowAttributes(hw, 0, 1, LWA_ALPHA);
    }
    window->show();
    window->setPanel("game", std::make_unique<GamePanel>(s));
    window->setPanel("keys", std::make_unique<KeysPanel>(s));
    int code = window->run(s.opt.frames);
    s.sender->releaseAll();
    s.dirty = true;
    s.writeState();
    return code;
}
