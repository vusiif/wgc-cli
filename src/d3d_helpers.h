#pragma once

#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <string>

struct D3DResources {
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgi_device;
};

D3DResources create_d3d_device();
std::wstring hresult_to_string(HRESULT hr);
