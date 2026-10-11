// SPDX-License-Identifier: Apache-2.0 OR MIT
#pragma once
#include <atomic>
#include <functional>
#include <string>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <d3d11.h>
#include <windows.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

namespace companion {

// Windows Graphics Capture of one window into D3D11 textures on the caller's device.
class Capture {
public:
    ~Capture();
    // onFrame runs on a capture thread each time a frame arrives.
    bool start(HWND window, ID3D11Device* device, std::function<void()> onFrame, std::string* error);
    void stop();
    // Hands the newest frame since the last call to use, which must finish with it before returning;
    // false when none arrived.
    bool next(const std::function<void(ID3D11Texture2D*, UINT, UINT)>& use);
    std::uint64_t frames() const { return frames_; }

private:
    winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice device_{nullptr};
    winrt::Windows::Graphics::Capture::GraphicsCaptureItem item_{nullptr};
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool pool_{nullptr};
    winrt::Windows::Graphics::Capture::GraphicsCaptureSession session_{nullptr};
    winrt::event_token arrived_{};
    winrt::Windows::Graphics::SizeInt32 poolSize_{};
    std::atomic<std::uint64_t> frames_{0};
};

}
