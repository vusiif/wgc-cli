#pragma once

#include "capture_wgc.h"
#include <string>

bool save_png(const CapturedImage& image, const std::wstring& path);
std::wstring generate_png_path(const std::wstring& dir, const std::wstring& title);
