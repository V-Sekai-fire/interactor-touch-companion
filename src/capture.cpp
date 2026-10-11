// SPDX-License-Identifier: Apache-2.0 OR MIT
#include "capture.h"

#include <dxgi.h>

#include <algorithm>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

namespace companion {

namespace wgc = winrt::Windows::Graphics::Capture;
namespace wdx = winrt::Windows::Graphics::DirectX;

Capture::~Capture() { stop(); }

bool Capture::start(HWND window, ID3D11Device* device, std::function<void()> onFrame, std::string* error) {
    try {
        winrt::com_ptr<IDXGIDevice> dxgi;
        winrt::check_hresult(device->QueryInterface(IID_PPV_ARGS(dxgi.put())));
        winrt::com_ptr<::IInspectable> inspectable;
        winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgi.get(), inspectable.put()));
        device_ = inspectable.as<wdx::Direct3D11::IDirect3DDevice>();
        auto interop = winrt::get_activation_factory<wgc::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
        winrt::check_hresult(interop->CreateForWindow(window, winrt::guid_of<wgc::GraphicsCaptureItem>(), winrt::put_abi(item_)));
        poolSize_ = item_.Size();
        pool_ = wgc::Direct3D11CaptureFramePool::CreateFreeThreaded(device_, wdx::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, poolSize_);
        arrived_ = pool_.FrameArrived([this, onFrame](auto&&, auto&&) {
            ++frames_;
            if (onFrame) onFrame();
        });
        session_ = pool_.CreateCaptureSession(item_);
        try {
            session_.IsCursorCaptureEnabled(false);
            session_.IsBorderRequired(false);
        } catch (const winrt::hresult_error&) {
        }
        session_.StartCapture();
        return true;
    } catch (const winrt::hresult_error& e) {
        if (error) *error = "Windows Graphics Capture: " + winrt::to_string(e.message());
        return false;
    }
}

void Capture::stop() {
    if (pool_) pool_.FrameArrived(arrived_);
    if (session_) session_.Close();
    if (pool_) pool_.Close();
    session_ = nullptr;
    pool_ = nullptr;
    item_ = nullptr;
}

bool Capture::next(const std::function<void(ID3D11Texture2D*, UINT, UINT)>& use) {
    if (!pool_) return false;
    wgc::Direct3D11CaptureFrame frame{nullptr};
    for (wgc::Direct3D11CaptureFrame f = pool_.TryGetNextFrame(); f; f = pool_.TryGetNextFrame()) frame = f;
    if (!frame) return false;
    winrt::Windows::Graphics::SizeInt32 size = frame.ContentSize();
    if (size.Width != poolSize_.Width || size.Height != poolSize_.Height) {
        poolSize_ = size;
        pool_.Recreate(device_, wdx::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, size);
    }
    auto access = frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
    winrt::com_ptr<ID3D11Texture2D> texture;
    winrt::check_hresult(access->GetInterface(IID_PPV_ARGS(texture.put())));
    D3D11_TEXTURE2D_DESC d{};
    texture->GetDesc(&d);
    use(texture.get(), std::min<UINT>(d.Width, static_cast<UINT>(size.Width)), std::min<UINT>(d.Height, static_cast<UINT>(size.Height)));
    return true;
}

}
