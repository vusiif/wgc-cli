#pragma once

#include <d3d11.h>
#include <cstdint>
#include <vector>
#include <optional>
#include <string>

struct CapturedImage {
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    std::vector<uint8_t> bgra;
};

struct CaptureResult {
    std::optional<CapturedImage> image;
    bool ok = false;
    std::wstring error_code;
    std::wstring message;
    std::wstring stage;
    std::wstring hresult;
    std::wstring suggestion;
};

CaptureResult capture_window(ID3D11Device* device, HWND hwnd, uint32_t timeout_ms);
