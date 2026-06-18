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

static GUID format_guid(const std::wstring& fmt) {
    if (fmt == L"jpg" || fmt == L"jpeg") return GUID_ContainerFormatJpeg;
    if (fmt == L"bmp") return GUID_ContainerFormatBmp;
    return GUID_ContainerFormatPng;
}

bool save_image(const CapturedImage& image, const std::wstring& path, const std::wstring& format) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
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

    GUID container_format = format_guid(format);
    ComPtr<IWICBitmapEncoder> encoder;
    hr = factory->CreateEncoder(container_format, nullptr, &encoder);
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

    WICPixelFormatGUID pix_fmt = GUID_WICPixelFormat32bppBGRA;
    hr = frame->SetPixelFormat(&pix_fmt);
    if (FAILED(hr)) return false;

    // JPEG quality
    if (container_format == GUID_ContainerFormatJpeg) {
        ComPtr<IPropertyBag2> props;
        frame->QueryInterface(IID_PPV_ARGS(&props));
        if (props) {
            PROPBAG2 opt{};
            opt.pstrName = const_cast<LPOLESTR>(L"ImageQuality");
            VARIANT val;
            VariantInit(&val);
            val.vt = VT_R4;
            val.fltVal = 0.85f;
            props->Write(1, &opt, &val);
        }
    }

    uint32_t stride = image.stride;
    uint32_t height = image.height;
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

CapturedImage resize_image(const CapturedImage& image, uint32_t target_w, uint32_t target_h) {
    CapturedImage result;
    result.width = target_w;
    result.height = target_h;
    result.stride = target_w * 4;
    result.bgra.resize(result.stride * target_h);

    // Bilinear interpolation
    float sx = static_cast<float>(image.width) / target_w;
    float sy = static_cast<float>(image.height) / target_h;

    for (uint32_t dy = 0; dy < target_h; ++dy) {
        float src_y = dy * sy;
        uint32_t y0 = static_cast<uint32_t>(src_y);
        uint32_t y1 = std::min(y0 + 1, image.height - 1);
        float fy = src_y - y0;

        for (uint32_t dx = 0; dx < target_w; ++dx) {
            float src_x = dx * sx;
            uint32_t x0 = static_cast<uint32_t>(src_x);
            uint32_t x1 = std::min(x0 + 1, image.width - 1);
            float fx = src_x - x0;

            auto* p00 = image.bgra.data() + y0 * image.stride + x0 * 4;
            auto* p10 = image.bgra.data() + y0 * image.stride + x1 * 4;
            auto* p01 = image.bgra.data() + y1 * image.stride + x0 * 4;
            auto* p11 = image.bgra.data() + y1 * image.stride + x1 * 4;

            auto* dst = result.bgra.data() + dy * result.stride + dx * 4;
            for (int c = 0; c < 4; ++c) {
                float v = p00[c] * (1 - fx) * (1 - fy)
                        + p10[c] * fx * (1 - fy)
                        + p01[c] * (1 - fx) * fy
                        + p11[c] * fx * fy;
                dst[c] = static_cast<uint8_t>(std::clamp(v, 0.0f, 255.0f));
            }
        }
    }
    return result;
}

CapturedImage crop_image(const CapturedImage& image, uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    // Clamp to image bounds
    if (x >= image.width) x = image.width - 1;
    if (y >= image.height) y = image.height - 1;
    if (x + w > image.width) w = image.width - x;
    if (y + h > image.height) h = image.height - y;

    CapturedImage result;
    result.width = w;
    result.height = h;
    result.stride = w * 4;
    result.bgra.resize(result.stride * h);

    for (uint32_t row = 0; row < h; ++row) {
        memcpy(result.bgra.data() + row * result.stride,
               image.bgra.data() + (y + row) * image.stride + x * 4,
               w * 4);
    }
    return result;
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

    while (!result.empty() && result.back() == L' ') result.pop_back();
    while (!result.empty() && result.front() == L' ') result.erase(result.begin());
    if (result.size() > 80) result.resize(80);
    if (result.empty()) result = L"window";

    return result;
}

std::wstring generate_output_path(const std::wstring& dir, const std::wstring& title, const std::wstring& format) {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    struct tm tm{};
    localtime_s(&tm, &time_t);

    std::wstring ext = (format == L"jpg" || format == L"jpeg") ? L".jpg"
                     : (format == L"bmp") ? L".bmp" : L".png";

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
        << ext;

    std::filesystem::path p(dir);
    p /= oss.str();
    return p.wstring();
}
