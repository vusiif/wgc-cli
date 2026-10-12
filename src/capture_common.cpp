#include "capture_common.h"
#include "capture_wgc.h"
#include "d3d_helpers.h"
#include "image_wic.h"

#include <windows.h>
#include <filesystem>

std::wstring format_from_extension(const std::wstring& path) {
    auto ext = std::filesystem::path(path).extension().wstring();
    for (auto& c : ext) c = towlower(c);
    if (ext == L".jpg" || ext == L".jpeg") return L"jpg";
    if (ext == L".bmp") return L"bmp";
    if (ext == L".png") return L"png";
    return L"";
}

OutputTarget resolve_output_target(const CaptureRequest& req) {
    std::wstring format = req.format;
    auto ext_fmt = format_from_extension(req.out);
    const bool is_file = !std::filesystem::is_directory(req.out) &&
        (!ext_fmt.empty() || std::filesystem::is_regular_file(req.out));
    if (!req.format_explicit && is_file && !ext_fmt.empty()) format = ext_fmt;
    return {is_file ? req.out : generate_output_path(req.out, req.title, format), format};
}

CaptureToFileResult capture_to_file(const CaptureRequest& req) {
    CaptureToFileResult result;

    // Restore minimized window
    if (req.restore_minimized && IsIconic(req.hwnd)) {
        ShowWindow(req.hwnd, SW_RESTORE);
        Sleep(500);
        for (int i = 0; i < 10 && IsIconic(req.hwnd); ++i) Sleep(100);
    }

    // Create D3D device
    auto d3d = create_d3d_device();
    if (!d3d.device) {
        result.error_code = L"D3D11_INIT_FAILED";
        result.message = L"Failed to create D3D11 device";
        return result;
    }

    // Capture
    auto capture_result = capture_window(d3d.device.Get(), req.hwnd, req.timeout_ms);
    if (!capture_result.ok) {
        result.error_code = capture_result.error_code;
        result.message = capture_result.message;
        result.stage = capture_result.stage;
        result.hresult = capture_result.hresult;
        result.suggestion = capture_result.suggestion;
        return result;
    }
    CapturedImage captured = std::move(*capture_result.image);

    // Crop
    if (req.has_crop) {
        if (req.crop_w == 0 || req.crop_h == 0) {
            result.error_code = L"CROP_INVALID_SIZE";
            result.message = L"Crop width and height must be > 0";
            return result;
        }
        if (req.crop_x >= captured.width || req.crop_y >= captured.height) {
            result.error_code = L"CROP_OUT_OF_BOUNDS";
            result.message = L"Crop origin outside image";
            return result;
        }
        captured = crop_image(captured, req.crop_x, req.crop_y, req.crop_w, req.crop_h);
    }

    // Resize
    if (req.resize_w > 0 && req.resize_h > 0) {
        captured = resize_image(captured, req.resize_w, req.resize_h);
    } else if (req.max_width > 0 && captured.width > req.max_width) {
        uint32_t target_h = captured.height * req.max_width / captured.width;
        captured = resize_image(captured, req.max_width, target_h);
    }

    auto output = resolve_output_target(req);
    const auto& out_path = output.path;
    auto parent = std::filesystem::path(out_path).parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent);

    // Save
    if (!save_image(captured, out_path, output.format)) {
        result.error_code = L"SAVE_FAILED";
        result.message = L"Failed to save image to " + out_path;
        return result;
    }

    result.ok = true;
    result.out_path = out_path;
    result.image_width = captured.width;
    result.image_height = captured.height;
    return result;
}
