#include "d3d_helpers.h"
#include <windows.h>

D3DResources create_d3d_device() {
    D3DResources res{};

    D3D_FEATURE_LEVEL feature_levels[] = { D3D_FEATURE_LEVEL_11_0 };
    D3D_FEATURE_LEVEL feature_level;

    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

    HRESULT hr = D3D11CreateDevice(
        nullptr,                    // adapter
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,                    // software module
        flags,
        feature_levels, 1,
        D3D11_SDK_VERSION,
        &res.device,
        &feature_level,
        &res.context
    );

    if (FAILED(hr)) {
        return {};
    }

    hr = res.device.As(&res.dxgi_device);
    if (FAILED(hr)) {
        return {};
    }

    return res;
}

std::wstring hresult_to_string(HRESULT hr) {
    wchar_t* msg = nullptr;
    DWORD len = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, hr,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&msg), 0, nullptr
    );

    std::wstring result;
    if (len > 0 && msg) {
        result.assign(msg, len);
        // trim trailing newline
        while (!result.empty() && (result.back() == L'\n' || result.back() == L'\r')) {
            result.pop_back();
        }
        LocalFree(msg);
    } else {
        result = L"HRESULT 0x" + std::to_wstring(static_cast<unsigned long>(hr));
    }
    return result;
}
