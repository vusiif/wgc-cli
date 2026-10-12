#pragma once

#include <windows.h>
#include <string>
#include <cstdint>

struct CaptureRequest {
    HWND hwnd = nullptr;
    std::wstring title;  // for output filename generation
    std::wstring out;
    std::wstring format = L"png";
    bool format_explicit = false;
    uint32_t timeout_ms = 3000;
    bool restore_minimized = true;
    // Crop
    uint32_t crop_x = 0, crop_y = 0, crop_w = 0, crop_h = 0;
    bool has_crop = false;
    // Resize
    uint32_t resize_w = 0, resize_h = 0;
    uint32_t max_width = 0;
};

struct CaptureToFileResult {
    bool ok = false;
    std::wstring out_path;
    uint32_t image_width = 0;
    uint32_t image_height = 0;
    // Error info
    std::wstring error_code;
    std::wstring message;
    std::wstring stage;
    std::wstring hresult;
    std::wstring suggestion;
};

// Core capture pipeline: D3D device -> capture -> crop -> resize -> infer format -> save
CaptureToFileResult capture_to_file(const CaptureRequest& req);

// Infer format from file extension (e.g. ".jpg" -> "jpg")
// Returns empty string if extension not recognized
std::wstring format_from_extension(const std::wstring& path);

struct OutputTarget {
    std::wstring path;
    std::wstring format;
};
OutputTarget resolve_output_target(const CaptureRequest& req);
