#pragma once

#include "capture_wgc.h"
#include <string>

bool save_image(const CapturedImage& image, const std::wstring& path, const std::wstring& format);
CapturedImage resize_image(const CapturedImage& image, uint32_t target_w, uint32_t target_h);
CapturedImage crop_image(const CapturedImage& image, uint32_t x, uint32_t y, uint32_t w, uint32_t h);
std::wstring generate_output_path(const std::wstring& dir, const std::wstring& title, const std::wstring& format);
