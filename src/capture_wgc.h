#pragma once

#include <d3d11.h>
#include <cstdint>
#include <vector>
#include <optional>

struct CapturedImage {
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    std::vector<uint8_t> bgra;
};

std::optional<CapturedImage> capture_window(ID3D11Device* device, HWND hwnd, uint32_t timeout_ms);
