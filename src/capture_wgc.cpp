#include "capture_wgc.h"
#include "d3d_helpers.h"

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#define _SILENCE_EXPERIMENTAL_COROUTINE_DEPRECATION_WARNINGS
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

#include <Windows.Graphics.Capture.Interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

using Microsoft::WRL::ComPtr;

namespace wgc = winrt::Windows::Graphics::Capture;
namespace wgdx = winrt::Windows::Graphics::DirectX;
namespace wgdxdd = winrt::Windows::Graphics::DirectX::Direct3D11;

static wgdxdd::IDirect3DDevice make_winrt_device(ID3D11Device* device) {
    ComPtr<IDXGIDevice> dxgi_device;
    HRESULT hr = device->QueryInterface(IID_PPV_ARGS(&dxgi_device));
    if (FAILED(hr)) return nullptr;

    wgdxdd::IDirect3DDevice rt_device{ nullptr };
    hr = CreateDirect3D11DeviceFromDXGIDevice(dxgi_device.Get(),
        reinterpret_cast<IInspectable**>(winrt::put_abi(rt_device)));
    if (FAILED(hr)) return nullptr;

    return rt_device;
}

static ComPtr<ID3D11Texture2D> get_texture_from_surface(wgdxdd::IDirect3DSurface surface) {
    // IDirect3DDxgiInterfaceAccess is defined in the Win32 C++ namespace by the interop header,
    // not in the winrt:: namespace. We must QI through the raw ABI pointer.
    IUnknown* unknown = static_cast<IUnknown*>(winrt::get_abi(surface));
    ::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess* dxgi_access = nullptr;
    HRESULT hr = unknown->QueryInterface(
        __uuidof(::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess),
        reinterpret_cast<void**>(&dxgi_access));
    if (FAILED(hr)) return nullptr;

    ComPtr<ID3D11Texture2D> texture;
    hr = dxgi_access->GetInterface(IID_PPV_ARGS(&texture));
    dxgi_access->Release();
    return SUCCEEDED(hr) ? texture : nullptr;
}

std::optional<CapturedImage> capture_window(ID3D11Device* device, HWND hwnd, uint32_t timeout_ms) {
    try {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
    } catch (...) {
        // already initialized, fine
    }

    // Create WinRT device
    auto rt_device = make_winrt_device(device);
    if (!rt_device) {
        return std::nullopt;
    }

    // Create GraphicsCaptureItem from HWND
    auto interop = winrt::get_activation_factory<wgc::GraphicsCaptureItem>()
        .as<IGraphicsCaptureItemInterop>();

    wgc::GraphicsCaptureItem item{ nullptr };
    HRESULT hr = interop->CreateForWindow(hwnd,
        winrt::guid_of<wgc::GraphicsCaptureItem>(),
        winrt::put_abi(item));

    if (FAILED(hr) || !item) {
        return std::nullopt;
    }

    auto size = item.Size();

    // Create frame pool
    auto frame_pool = wgc::Direct3D11CaptureFramePool::CreateFreeThreaded(
        rt_device,
        wgdx::DirectXPixelFormat::B8G8R8A8UIntNormalized,
        1,
        size);

    // Create capture session
    auto session = frame_pool.CreateCaptureSession(item);
    session.IsBorderRequired(false);
    session.IsCursorCaptureEnabled(false);

    // Wait for first frame
    winrt::handle frame_event{ CreateEvent(nullptr, TRUE, FALSE, nullptr) };
    std::optional<CapturedImage> result;

    frame_pool.FrameArrived([&](wgc::Direct3D11CaptureFramePool const& pool,
                                 winrt::Windows::Foundation::IInspectable const&) {
        auto frame = pool.TryGetNextFrame();
        if (!frame) return;

        auto frame_size = frame.ContentSize();
        auto surface = frame.Surface();

        auto texture = get_texture_from_surface(surface);
        if (!texture) return;

        D3D11_TEXTURE2D_DESC desc{};
        texture->GetDesc(&desc);

        // Create staging texture
        D3D11_TEXTURE2D_DESC staging_desc{};
        staging_desc.Width = frame_size.Width;
        staging_desc.Height = frame_size.Height;
        staging_desc.MipLevels = 1;
        staging_desc.ArraySize = 1;
        staging_desc.Format = desc.Format;
        staging_desc.SampleDesc.Count = 1;
        staging_desc.Usage = D3D11_USAGE_STAGING;
        staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

        ComPtr<ID3D11Texture2D> staging;
        ComPtr<ID3D11Device> tex_device;
        texture->GetDevice(&tex_device);
        hr = tex_device->CreateTexture2D(&staging_desc, nullptr, &staging);
        if (FAILED(hr)) return;

        ComPtr<ID3D11DeviceContext> ctx;
        tex_device->GetImmediateContext(&ctx);
        ctx->CopyResource(staging.Get(), texture.Get());

        D3D11_MAPPED_SUBRESOURCE mapped{};
        hr = ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
        if (FAILED(hr)) return;

        CapturedImage img{};
        img.width = static_cast<uint32_t>(frame_size.Width);
        img.height = static_cast<uint32_t>(frame_size.Height);
        img.stride = mapped.RowPitch;
        img.bgra.resize(mapped.RowPitch * frame_size.Height);
        memcpy(img.bgra.data(), mapped.pData, img.bgra.size());

        ctx->Unmap(staging.Get(), 0);

        result = std::move(img);
        SetEvent(frame_event.get());
    });

    session.StartCapture();

    // Wait for frame with timeout
    DWORD wait_result = WaitForSingleObject(frame_event.get(), timeout_ms);
    session.Close();
    frame_pool.Close();

    if (wait_result != WAIT_OBJECT_0 || !result) {
        return std::nullopt;
    }

    return result;
}
