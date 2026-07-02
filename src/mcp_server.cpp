#include "mcp_server.h"
#include "window_enum.h"
#include "capture_wgc.h"
#include "d3d_helpers.h"
#include "image_wic.h"

#include <windows.h>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <filesystem>
#include <iomanip>

// --- JSON helpers ---

static std::wstring escape_json(const std::wstring& s) {
    std::wostringstream oss;
    for (wchar_t c : s) {
        switch (c) {
            case L'"':  oss << L"\\\""; break;
            case L'\\': oss << L"\\\\"; break;
            case L'\n': oss << L"\\n";  break;
            case L'\r': oss << L"\\r";  break;
            case L'\t': oss << L"\\t";  break;
            default:
                if (c < 0x20) {
                    std::wostringstream hex;
                    hex << L"\\u" << std::setfill(L'0') << std::setw(4)
                        << std::hex << static_cast<int>(c);
                    oss << hex.str();
                } else {
                    oss << c;
                }
                break;
        }
    }
    return oss.str();
}

static std::wstring hwnd_hex(HWND hwnd) {
    std::wostringstream oss;
    oss << L"0x" << std::uppercase << std::setfill(L'0')
        << std::setw(16) << std::hex << reinterpret_cast<uintptr_t>(hwnd);
    return oss.str();
}

// Minimal JSON key extraction (handles escaped values)
static std::wstring json_get_string(const std::wstring& json, const std::wstring& key) {
    std::wstring needle = L"\"" + key + L"\":";
    auto pos = json.find(needle);
    if (pos == std::wstring::npos) return L"";
    pos += needle.size();
    while (pos < json.size() && json[pos] == L' ') pos++;
    if (pos >= json.size() || json[pos] != L'"') return L"";
    pos++;
    std::wstring result;
    while (pos < json.size()) {
        if (json[pos] == L'\\' && pos + 1 < json.size()) {
            wchar_t next = json[pos + 1];
            if (next == L'"') { result += L'"'; pos += 2; }
            else if (next == L'\\') { result += L'\\'; pos += 2; }
            else if (next == L'n') { result += L'\n'; pos += 2; }
            else if (next == L'r') { result += L'\r'; pos += 2; }
            else if (next == L't') { result += L'\t'; pos += 2; }
            else { result += next; pos += 2; }
        } else if (json[pos] == L'"') {
            break;
        } else {
            result += json[pos];
            pos++;
        }
    }
    return result;
}

static int64_t json_get_int(const std::wstring& json, const std::wstring& key) {
    std::wstring needle = L"\"" + key + L"\":";
    auto pos = json.find(needle);
    if (pos == std::wstring::npos) return -1;
    pos += needle.size();
    while (pos < json.size() && json[pos] == L' ') pos++;
    if (pos >= json.size()) return -1;
    return std::wcstoll(json.c_str() + pos, nullptr, 10);
}

static std::wstring json_get_raw(const std::wstring& json, const std::wstring& key) {
    std::wstring needle = L"\"" + key + L"\":";
    auto pos = json.find(needle);
    if (pos == std::wstring::npos) return L"";
    pos += needle.size();
    while (pos < json.size() && json[pos] == L' ') pos++;
    if (pos >= json.size()) return L"";
    // Track nesting depth and string state to find end of value
    int depth = 0;
    bool in_string = false;
    bool escaped = false;
    size_t start = pos;
    for (size_t i = pos; i < json.size(); i++) {
        wchar_t c = json[i];
        if (escaped) { escaped = false; continue; }
        if (c == L'\\' && in_string) { escaped = true; continue; }
        if (c == L'"') { in_string = !in_string; continue; }
        if (in_string) continue;
        if (c == L'{' || c == L'[') depth++;
        else if (c == L'}' || c == L']') {
            depth--;
            if (depth < 0) return json.substr(start, i - start);
        }
        else if (c == L',' && depth == 0) return json.substr(start, i - start);
    }
    return json.substr(start);
}

// --- stdin/stdout I/O ---

static bool read_line(std::wstring& out) {
    out.clear();
    // Read UTF-8 from stdin
    char buf[65536];
    if (!fgets(buf, sizeof(buf), stdin)) return false;
    // Convert to wstring
    int len = MultiByteToWideChar(CP_UTF8, 0, buf, -1, nullptr, 0);
    if (len <= 0) return false;
    out.resize(len - 1);
    MultiByteToWideChar(CP_UTF8, 0, buf, -1, out.data(), len);
    // Trim trailing \n/\r
    while (!out.empty() && (out.back() == L'\n' || out.back() == L'\r'))
        out.pop_back();
    return !out.empty();
}

static void write_response(const std::wstring& json) {
    std::string utf8;
    int len = WideCharToMultiByte(CP_UTF8, 0, json.c_str(),
                                   static_cast<int>(json.size()),
                                   nullptr, 0, nullptr, nullptr);
    if (len > 0) {
        utf8.resize(len);
        WideCharToMultiByte(CP_UTF8, 0, json.c_str(),
                            static_cast<int>(json.size()),
                            utf8.data(), len, nullptr, nullptr);
    }
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD written = 0;
    WriteFile(hOut, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    // Newline delimiter
    const char nl = '\n';
    WriteFile(hOut, &nl, 1, &written, nullptr);
    FlushFileBuffers(hOut);
}

// --- MCP protocol handlers ---

static std::wstring make_response(int64_t id, const std::wstring& result) {
    std::wostringstream oss;
    oss << L"{\"jsonrpc\":\"2.0\",\"id\":" << id
        << L",\"result\":" << result << L"}";
    return oss.str();
}

static std::wstring make_error(int64_t id, int code, const std::wstring& message) {
    std::wostringstream oss;
    oss << L"{\"jsonrpc\":\"2.0\",\"id\":" << id
        << L",\"error\":{\"code\":" << code
        << L",\"message\":\"" << escape_json(message) << L"\"}}";
    return oss.str();
}

static std::wstring handle_initialize(int64_t id) {
    return make_response(id,
        L"{\"protocolVersion\":\"2024-11-05\""
        L",\"capabilities\":{\"tools\":{}}"
        L",\"serverInfo\":{\"name\":\"wgccli\",\"version\":\"1.7.0\"}}");
}

static std::wstring handle_tools_list(int64_t id) {
    return make_response(id,
        L"{\"tools\":["
        L"{\"name\":\"list_windows\""
        L",\"description\":\"List all visible top-level windows\""
        L",\"inputSchema\":{\"type\":\"object\",\"properties\":{"
        L"\"include_minimized\":{\"type\":\"boolean\",\"description\":\"Include minimized windows (default true)\"}"
        L"},\"required\":[]}}"
        L","
        L"{\"name\":\"capture_window\""
        L",\"description\":\"Capture a screenshot of a window by title, HWND, PID, or process name\""
        L",\"inputSchema\":{\"type\":\"object\",\"properties\":{"
        L"\"title\":{\"type\":\"string\",\"description\":\"Window title (partial match, case-insensitive)\"},"
        L"\"hwnd\":{\"type\":\"string\",\"description\":\"Window handle (hex, e.g. 0x12345)\"},"
        L"\"pid\":{\"type\":\"integer\",\"description\":\"Process ID\"},"
        L"\"process\":{\"type\":\"string\",\"description\":\"Process exe name (e.g. chrome.exe)\"},"
        L"\"class_name\":{\"type\":\"string\",\"description\":\"Window class name\"},"
        L"\"out\":{\"type\":\"string\",\"description\":\"Output directory or file path\"},"
        L"\"format\":{\"type\":\"string\",\"enum\":[\"png\",\"jpg\",\"jpeg\",\"bmp\"],\"description\":\"Output format (default png)\"},"
        L"\"max_width\":{\"type\":\"integer\",\"description\":\"Scale down to max width\"},"
        L"\"resize\":{\"type\":\"string\",\"description\":\"Resize to WxH (e.g. 1024x768)\"},"
        L"\"crop\":{\"type\":\"string\",\"description\":\"Crop region x,y,w,h\"},"
        L"\"timeout_ms\":{\"type\":\"integer\",\"description\":\"Capture timeout in ms (default 3000)\"},"
        L"\"exact\":{\"type\":\"boolean\",\"description\":\"Exact title match\"}"
        L"},\"required\":[\"out\"]}}"
        L"]}");
}

static std::wstring do_list_windows(const std::wstring& params) {
    bool include_min = true;
    auto inc = json_get_string(params, L"include_minimized");
    if (inc == L"false") include_min = false;

    auto windows = enumerate_windows(include_min);
    // Build JSON array of windows, then embed as escaped text
    std::wostringstream arr;
    arr << L"[";
    bool first = true;
    for (const auto& w : windows) {
        if (!w.visible) continue;
        int ww = w.rect.right - w.rect.left;
        int wh = w.rect.bottom - w.rect.top;
        if (ww <= 1 && wh <= 1 && !w.minimized) continue;
        if (!first) arr << L",";
        first = false;
        arr << L"{\"hwnd\":\"" << hwnd_hex(w.hwnd)
            << L"\",\"pid\":" << w.pid
            << L",\"title\":\"" << escape_json(w.title)
            << L"\",\"className\":\"" << escape_json(w.className)
            << L"\",\"width\":" << ww
            << L",\"height\":" << wh
            << L",\"minimized\":" << (w.minimized ? L"true" : L"false")
            << L"}";
    }
    arr << L"]";
    std::wostringstream oss;
    oss << L"{\"content\":[{\"type\":\"text\",\"text\":"
        << L"\"" << escape_json(arr.str()) << L"\"}]}";
    return oss.str();
}

static std::wstring do_capture_window(const std::wstring& params) {
    auto title = json_get_string(params, L"title");
    auto hwnd_str = json_get_string(params, L"hwnd");
    auto out = json_get_string(params, L"out");
    auto process = json_get_string(params, L"process");
    auto className = json_get_string(params, L"class_name");
    auto format = json_get_string(params, L"format");
    auto resize_str = json_get_string(params, L"resize");
    auto crop_str = json_get_string(params, L"crop");
    int64_t pid_val = json_get_int(params, L"pid");
    int64_t max_width = json_get_int(params, L"max_width");
    int64_t timeout = json_get_int(params, L"timeout_ms");
    bool exact = json_get_string(params, L"exact") == L"true";

    if (out.empty()) {
        return L"{\"content\":[{\"type\":\"text\",\"text\":\"Error: out parameter is required\"}],\"isError\":true}";
    }

    if (format.empty()) format = L"png";

    uint32_t pid = pid_val > 0 ? static_cast<uint32_t>(pid_val) : 0;
    uint32_t timeout_ms = timeout > 0 ? static_cast<uint32_t>(timeout) : 3000;
    uint32_t mw = max_width > 0 ? static_cast<uint32_t>(max_width) : 0;

    // Parse resize
    uint32_t resize_w = 0, resize_h = 0;
    if (!resize_str.empty()) {
        auto xpos = resize_str.find(L'x');
        if (xpos == std::wstring::npos) xpos = resize_str.find(L'X');
        if (xpos != std::wstring::npos && xpos > 0 && xpos < resize_str.size() - 1) {
            resize_w = static_cast<uint32_t>(std::wcstoul(resize_str.c_str(), nullptr, 10));
            resize_h = static_cast<uint32_t>(std::wcstoul(resize_str.c_str() + xpos + 1, nullptr, 10));
        }
    }

    // Parse crop
    uint32_t crop_x = 0, crop_y = 0, crop_w = 0, crop_h = 0;
    bool has_crop = false;
    if (!crop_str.empty()) {
        const wchar_t* p = crop_str.c_str();
        wchar_t* end = nullptr;
        uint32_t parts[4] = {};
        for (int j = 0; j < 4; ++j) {
            if (*p == L'\0') break;
            parts[j] = static_cast<uint32_t>(std::wcstoul(p, &end, 10));
            p = end;
            if (j < 3 && *p == L',') p++;
        }
        crop_x = parts[0]; crop_y = parts[1];
        crop_w = parts[2]; crop_h = parts[3];
        has_crop = (crop_w > 0 && crop_h > 0);
    }

    // Parse hwnd
    uint64_t hwnd_val = 0;
    if (!hwnd_str.empty()) {
        if (hwnd_str.size() > 2 && hwnd_str[0] == L'0' && (hwnd_str[1] == L'x' || hwnd_str[1] == L'X'))
            hwnd_val = std::wcstoull(hwnd_str.c_str(), nullptr, 16);
        else
            hwnd_val = std::wcstoull(hwnd_str.c_str(), nullptr, 10);
    }

    // Find target window
    WindowInfo target{};
    bool found = false;

    if (hwnd_val != 0) {
        target = find_window_by_hwnd(hwnd_val);
        found = target.hwnd != nullptr;
    } else {
        auto windows = enumerate_windows(true);
        auto filtered = filter_windows(windows, pid, pid != 0,
                                       process, !process.empty(),
                                       className, !className.empty());
        if (!title.empty()) {
            auto match = find_best_match(filtered, title, exact);
            if (match) { target = *match; found = true; }
        } else if (filtered.size() == 1) {
            target = filtered[0];
            found = true;
        } else if (filtered.size() > 1) {
            std::wostringstream oss;
            oss << L"{\"content\":[{\"type\":\"text\",\"text\":\""
                << escape_json(std::to_wstring(filtered.size()) + L" windows matched. Use title or hwnd to select one.")
                << L"\"}],\"isError\":true}";
            return oss.str();
        }
    }

    if (!found) {
        std::wstring desc;
        if (hwnd_val) desc = L"hwnd " + hwnd_str;
        else if (!title.empty()) desc = L"title: " + title;
        else if (pid) desc = L"pid " + std::to_wstring(pid);
        else if (!process.empty()) desc = L"process: " + process;
        else if (!className.empty()) desc = L"class: " + className;
        std::wostringstream oss;
        oss << L"{\"content\":[{\"type\":\"text\",\"text\":\""
            << escape_json(L"No window matched " + desc)
            << L"\"}],\"isError\":true}";
        return oss.str();
    }

    // Restore if minimized
    if (target.minimized) {
        ShowWindow(target.hwnd, SW_RESTORE);
        Sleep(500);
        for (int i = 0; i < 10 && IsIconic(target.hwnd); ++i) Sleep(100);
    }

    auto d3d = create_d3d_device();
    if (!d3d.device) {
        return L"{\"content\":[{\"type\":\"text\",\"text\":\"D3D11 device creation failed\"}],\"isError\":true}";
    }

    auto capture_result = capture_window(d3d.device.Get(), target.hwnd, timeout_ms);
    if (!capture_result.ok) {
        std::wostringstream oss;
        oss << L"{\"content\":[{\"type\":\"text\",\"text\":\""
            << escape_json(capture_result.error_code + L": " + capture_result.message)
            << L"\"}],\"isError\":true}";
        return oss.str();
    }

    CapturedImage captured = std::move(*capture_result.image);

    // Crop
    if (has_crop) {
        if (crop_x >= captured.width || crop_y >= captured.height) {
            return L"{\"content\":[{\"type\":\"text\",\"text\":\"Crop origin outside image\"}],\"isError\":true}";
        }
        captured = crop_image(captured, crop_x, crop_y, crop_w, crop_h);
    }

    // Resize
    if (resize_w > 0 && resize_h > 0) {
        captured = resize_image(captured, resize_w, resize_h);
    } else if (mw > 0 && captured.width > mw) {
        uint32_t th = captured.height * mw / captured.width;
        captured = resize_image(captured, mw, th);
    }

    // Generate output path
    std::wstring out_path;
    std::wstring ext_check = out.size() >= 4 ? out.substr(out.size() - 4) : L"";
    bool is_file = (ext_check == L".png" || ext_check == L".PNG"
                 || ext_check == L".jpg" || ext_check == L".JPG"
                 || ext_check == L".bmp" || ext_check == L".BMP");
    if (is_file) {
        out_path = out;
        auto parent = std::filesystem::path(out_path).parent_path();
        if (!parent.empty()) std::filesystem::create_directories(parent);
    } else {
        std::filesystem::create_directories(out);
        out_path = generate_output_path(out, target.title, format);
    }

    if (!save_image(captured, out_path, format)) {
        return L"{\"content\":[{\"type\":\"text\",\"text\":\"Failed to save image\"}],\"isError\":true}";
    }

    // Build success response
    int ww = target.rect.right - target.rect.left;
    int wh = target.rect.bottom - target.rect.top;
    std::wostringstream text;
    text << L"Screenshot saved: " << out_path
         << L"\nWindow: " << target.title
         << L" (" << ww << L"x" << wh << L")"
         << L"\nOutput: " << captured.width << L"x" << captured.height;

    std::wostringstream oss;
    oss << L"{\"content\":[{\"type\":\"text\",\"text\":\""
        << escape_json(text.str()) << L"\"}]}";
    return oss.str();
}

static std::wstring handle_tools_call(int64_t id, const std::wstring& params) {
    auto name = json_get_string(params, L"name");
    auto arguments = json_get_raw(params, L"arguments");

    std::wstring result;
    if (name == L"list_windows") {
        result = do_list_windows(arguments);
    } else if (name == L"capture_window") {
        result = do_capture_window(arguments);
    } else {
        return make_error(id, -32601, L"Unknown tool: " + name);
    }
    return make_response(id, result);
}

// --- Main entry ---

int run_mcp_server() {
    // Set stdin/stdout to UTF-8
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    std::wstring line;
    while (read_line(line)) {
        // Extract id and method
        int64_t id = json_get_int(line, L"id");
        auto method = json_get_string(line, L"method");

        std::wstring response;
        if (method == L"initialize") {
            response = handle_initialize(id);
        } else if (method == L"notifications/initialized") {
            // Notification, no response needed
            continue;
        } else if (method == L"tools/list") {
            response = handle_tools_list(id);
        } else if (method == L"tools/call") {
            auto params_start = line.find(L"\"params\"");
            if (params_start != std::wstring::npos) {
                // Extract the params object
                auto colon = line.find(L':', params_start);
                if (colon != std::wstring::npos) {
                    response = handle_tools_call(id, line.substr(colon + 1));
                }
            }
            if (response.empty()) {
                response = make_error(id, -32602, L"Invalid params");
            }
        } else if (method == L"ping") {
            response = make_response(id, L"{}");
        } else {
            response = make_error(id, -32601, L"Method not found: " + method);
        }

        write_response(response);
    }

    return 0;
}
