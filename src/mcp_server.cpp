#include "parse_utils.h"
#include "mcp_server.h"
#include "window_enum.h"
#include "capture_common.h"

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
    auto pos = json_value_position(json, key);
    if (pos == std::wstring::npos) return L"";
    std::wstring out;
    size_t end = 0;
    if (!json_decode_string(json, pos, out, end)) return L"";
    return out;
}

static bool json_get_bool(const std::wstring& json, const std::wstring& key) {
    auto pos = json_value_position(json, key);
    if (pos == std::wstring::npos) return false;
    return json_token(json, key) == L"true";
}

static int64_t json_get_int(const std::wstring& json, const std::wstring& key) {
    auto pos = json_value_position(json, key);
    if (pos == std::wstring::npos) return -1;
    if (pos >= json.size()) return -1;
    auto token = json_token(json, key);
    bool negative = !token.empty() && token[0] == L'-';
    uint64_t value = 0;
    auto digits = std::wstring_view(token);
    if (negative) digits.remove_prefix(1);
    if (!parse_unsigned(digits, value) || value > INT64_MAX) return -1;
    return negative ? -static_cast<int64_t>(value) : static_cast<int64_t>(value);
}

static std::wstring json_get_raw(const std::wstring& json, const std::wstring& key) {
    auto pos = json_value_position(json, key);
    if (pos == std::wstring::npos) return L"";
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

static std::wstring json_rpc_id(const std::wstring& json) {
    auto pos = json_value_position(json, L"id");
    if (pos == std::wstring::npos || pos >= json.size()) return L"-1";
    auto delimited = [&json](size_t i) {
        while (i < json.size() && json_space(json[i])) ++i;
        return i < json.size() && (json[i] == L',' || json[i] == L'}' || json[i] == L']');
    };
    wchar_t c = json[pos];
    if (c == L'"') {
        std::wstring decoded;
        size_t end = 0;
        if (!json_decode_string(json, pos, decoded, end) || !delimited(end)) return L"-1";
        return json.substr(pos, end - pos);
    }
    if (json.compare(pos, 4, L"null") == 0)
        return delimited(pos + 4) ? L"null" : L"-1";
    if (c != L'-' && (c < L'0' || c > L'9')) return L"-1";
    size_t i = pos;
    if (json[i] == L'-') ++i;
    if (i >= json.size() || json[i] < L'0' || json[i] > L'9') return L"-1";
    if (json[i] == L'0') ++i;
    else while (i < json.size() && json[i] >= L'0' && json[i] <= L'9') ++i;
    if (i < json.size() && json[i] == L'.') {
        ++i;
        size_t digits = i;
        while (i < json.size() && json[i] >= L'0' && json[i] <= L'9') ++i;
        if (i == digits) return L"-1";
    }
    if (i < json.size() && (json[i] == L'e' || json[i] == L'E')) {
        ++i;
        if (i < json.size() && (json[i] == L'+' || json[i] == L'-')) ++i;
        size_t digits = i;
        while (i < json.size() && json[i] >= L'0' && json[i] <= L'9') ++i;
        if (i == digits) return L"-1";
    }
    if (!delimited(i)) return L"-1";
    return json.substr(pos, i - pos);
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
    out.resize(len);
    MultiByteToWideChar(CP_UTF8, 0, buf, -1, out.data(), len);
    out.pop_back();
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

static std::wstring make_response(const std::wstring& id, const std::wstring& result) {
    std::wostringstream oss;
    oss << L"{\"jsonrpc\":\"2.0\",\"id\":" << id
        << L",\"result\":" << result << L"}";
    return oss.str();
}

static std::wstring make_error(const std::wstring& id, int code, const std::wstring& message) {
    std::wostringstream oss;
    oss << L"{\"jsonrpc\":\"2.0\",\"id\":" << id
        << L",\"error\":{\"code\":" << code
        << L",\"message\":\"" << escape_json(message) << L"\"}}";
    return oss.str();
}

static std::wstring handle_initialize(const std::wstring& id) {
    return make_response(id,
        L"{\"protocolVersion\":\"2024-11-05\""
        L",\"capabilities\":{\"tools\":{}}"
        L",\"serverInfo\":{\"name\":\"wgccli\",\"version\":\"1.7.4\"}}");
}

static std::wstring handle_tools_list(const std::wstring& id) {
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
    // Default true; only false if explicitly set to false
    bool has_min_key = json_value_position(params, L"include_minimized") != std::wstring::npos;
    bool include_min = !has_min_key || json_get_bool(params, L"include_minimized");

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
    const bool format_explicit = json_value_position(params, L"format") != std::wstring::npos;
    auto resize_str = json_get_string(params, L"resize");
    auto crop_str = json_get_string(params, L"crop");
    int64_t pid_val = json_get_int(params, L"pid");
    int64_t max_width = json_get_int(params, L"max_width");
    int64_t timeout = json_get_int(params, L"timeout_ms");
    bool exact = json_get_bool(params, L"exact");

    if (out.empty()) {
        return L"{\"content\":[{\"type\":\"text\",\"text\":\"Error: out parameter is required\"}],\"isError\":true}";
    }

    if (!format_explicit) format = L"png";
    for (auto& c : format) c = towlower(c);
    if (format != L"png" && format != L"jpg" && format != L"jpeg" && format != L"bmp")
        return L"{\"content\":[{\"type\":\"text\",\"text\":\"Invalid format\"}],\"isError\":true}";

    for (const auto& key : {L"pid", L"max_width", L"timeout_ms"}) {
        if (json_value_position(params, key) == std::wstring::npos) continue;
        uint32_t value = 0;
        if (!parse_u32_strict(json_token(params, key), value))
            return L"{\"content\":[{\"type\":\"text\",\"text\":\"Expected a uint32 integer\"}],\"isError\":true}";
    }

    uint32_t pid = pid_val > 0 ? static_cast<uint32_t>(pid_val) : 0;
    uint32_t timeout_ms = timeout > 0 ? static_cast<uint32_t>(timeout) : 3000;
    uint32_t mw = max_width > 0 ? static_cast<uint32_t>(max_width) : 0;

    // Parse resize
    uint32_t resize_w = 0, resize_h = 0;
    if (!resize_str.empty()) {
        auto xpos = resize_str.find(L'x');
        if (xpos == std::wstring::npos) xpos = resize_str.find(L'X');
        if (xpos == std::wstring::npos || xpos == 0 || xpos == resize_str.size() - 1 ||
            !parse_u32_strict(resize_str.substr(0, xpos), resize_w) ||
            !parse_u32_strict(resize_str.substr(xpos + 1), resize_h) ||
            resize_w == 0 || resize_h == 0) {
            return L"{\"content\":[{\"type\":\"text\",\"text\":\"Invalid resize format, use WxH (e.g. 1024x768)\"}],\"isError\":true}";
        }
    }

    // Parse crop
    uint32_t crop_x = 0, crop_y = 0, crop_w = 0, crop_h = 0;
    bool has_crop = false;
    if (!crop_str.empty()) {
        const wchar_t* p = crop_str.c_str();
        const wchar_t* end = nullptr;
        uint32_t parts[4] = {};
        for (int j = 0; j < 4; ++j) {
            if (*p == L'\0') {
                return L"{\"content\":[{\"type\":\"text\",\"text\":\"Crop requires 4 values (x,y,w,h)\"}],\"isError\":true}";
            }
            end = p;
            while (*end && *end != L',') ++end;
            uint32_t val = 0;
            if (!parse_u32_strict(std::wstring(p, static_cast<size_t>(end - p)), val)) {
                return L"{\"content\":[{\"type\":\"text\",\"text\":\"Invalid crop value\"}],\"isError\":true}";
            }
            parts[j] = static_cast<uint32_t>(val);
            p = end;
            if (j < 3) {
                if (*p != L',') {
                    return L"{\"content\":[{\"type\":\"text\",\"text\":\"Crop format is x,y,w,h\"}],\"isError\":true}";
                }
                p++;
            }
        }
        if (*p != L'\0') {
            return L"{\"content\":[{\"type\":\"text\",\"text\":\"Trailing characters in crop\"}],\"isError\":true}";
        }
        crop_x = parts[0]; crop_y = parts[1];
        crop_w = parts[2]; crop_h = parts[3];
        if (crop_w == 0 || crop_h == 0)
            return L"{\"content\":[{\"type\":\"text\",\"text\":\"Crop size must be positive\"}],\"isError\":true}";
        has_crop = true;
    }

    // Parse hwnd (strict: must fully consume)
    uint64_t hwnd_val = 0;
    if (json_value_position(params, L"hwnd") != std::wstring::npos &&
        !parse_hwnd_strict(hwnd_str, hwnd_val))
        return L"{\"content\":[{\"type\":\"text\",\"text\":\"Invalid HWND\"}],\"isError\":true}";

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

    // Capture via shared pipeline
    CaptureRequest req;
    req.hwnd = target.hwnd;
    req.title = target.title;
    req.out = out;
    req.format = format;
    req.format_explicit = format_explicit;
    req.timeout_ms = timeout_ms;
    req.has_crop = has_crop;
    req.crop_x = crop_x; req.crop_y = crop_y;
    req.crop_w = crop_w; req.crop_h = crop_h;
    req.resize_w = resize_w; req.resize_h = resize_h;
    req.max_width = mw;

    auto cr = capture_to_file(req);
    if (!cr.ok) {
        std::wostringstream oss;
        oss << L"{\"content\":[{\"type\":\"text\",\"text\":\""
            << escape_json(cr.error_code + L": " + cr.message)
            << L"\"}],\"isError\":true}";
        return oss.str();
    }

    // Build success response
    int ww = target.rect.right - target.rect.left;
    int wh = target.rect.bottom - target.rect.top;
    std::wostringstream text;
    text << L"Screenshot saved: " << cr.out_path
         << L"\nWindow: " << target.title
         << L" (" << ww << L"x" << wh << L")"
         << L"\nOutput: " << cr.image_width << L"x" << cr.image_height;

    std::wostringstream oss;
    oss << L"{\"content\":[{\"type\":\"text\",\"text\":\""
        << escape_json(text.str()) << L"\"}]}";
    return oss.str();
}

static std::wstring handle_tools_call(const std::wstring& id, const std::wstring& params) {
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
        std::wstring id = json_rpc_id(line);
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
            auto params_start = json_value_position(line, L"params");
            if (params_start != std::wstring::npos) {
                response = handle_tools_call(id, json_get_raw(line, L"params"));
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
