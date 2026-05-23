#include "image_wic.h"

#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <filesystem>
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <sstream>

using Microsoft::WRL::ComPtr;

bool save_png(const CapturedImage& image, const std::wstring& path) {
    ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(
        CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory));
    if (FAILED(hr)) return false;

    ComPtr<IWICStream> stream;
    hr = factory->CreateStream(&stream);
    if (FAILED(hr)) return false;

    hr = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
    if (FAILED(hr)) return false;

    ComPtr<IWICBitmapEncoder> encoder;
    hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    if (FAILED(hr)) return false;

    hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    if (FAILED(hr)) return false;

    ComPtr<IWICBitmapFrameEncode> frame;
    hr = encoder->CreateNewFrame(&frame, nullptr);
    if (FAILED(hr)) return false;

    hr = frame->Initialize(nullptr);
    if (FAILED(hr)) return false;

    hr = frame->SetSize(image.width, image.height);
    if (FAILED(hr)) return false;

    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    hr = frame->SetPixelFormat(&format);
    if (FAILED(hr)) return false;

    // Write lines
    uint32_t stride = image.stride;
    uint32_t height = image.height;
    // WIC expects stride = width * bpp for the source rect
    uint32_t wic_stride = image.width * 4;

    if (stride == wic_stride) {
        hr = frame->WritePixels(height, stride,
            static_cast<UINT>(stride * height),
            const_cast<BYTE*>(image.bgra.data()));
    } else {
        std::vector<BYTE> row(wic_stride);
        for (uint32_t y = 0; y < height; ++y) {
            memcpy(row.data(), image.bgra.data() + y * stride, wic_stride);
            hr = frame->WritePixels(1, wic_stride, wic_stride, row.data());
            if (FAILED(hr)) break;
        }
    }
    if (FAILED(hr)) return false;

    hr = frame->Commit();
    if (FAILED(hr)) return false;

    hr = encoder->Commit();
    if (FAILED(hr)) return false;

    return true;
}

static std::wstring sanitize_title(const std::wstring& title) {
    std::wstring result;
    result.reserve(title.size());

    for (wchar_t c : title) {
        switch (c) {
            case L'\\': case L'/': case L':': case L'*':
            case L'?': case L'"': case L'<': case L'>': case L'|':
                result += L'-';
                break;
            default:
                result += c;
                break;
        }
    }

    // Trim
    while (!result.empty() && result.back() == L' ') result.pop_back();
    while (!result.empty() && result.front() == L' ') result.erase(result.begin());

    // Limit length
    if (result.size() > 80) {
        result.resize(80);
    }

    if (result.empty()) {
        result = L"window";
    }

    return result;
}

std::wstring generate_png_path(const std::wstring& dir, const std::wstring& title) {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    struct tm tm{};
    localtime_s(&tm, &time_t);

    std::wostringstream oss;
    oss << std::setfill(L'0')
        << std::setw(4) << (tm.tm_year + 1900)
        << std::setw(2) << (tm.tm_mon + 1)
        << std::setw(2) << tm.tm_mday
        << L'-'
        << std::setw(2) << tm.tm_hour
        << std::setw(2) << tm.tm_min
        << std::setw(2) << tm.tm_sec
        << L'-'
        << sanitize_title(title)
        << L".png";

    std::filesystem::path p(dir);
    p /= oss.str();
    return p.wstring();
}
