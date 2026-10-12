#include "parse_utils.h"
#include "server.h"
#include "window_enum.h"
#include "capture_common.h"

#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#include <iostream>
#include <sstream>
#include <vector>
#include <filesystem>
#include <iomanip>

// Build DACL: current user + LocalSystem only
// Returns true on success. Caller must free *out_acl with LocalFree when done.
static bool build_pipe_security(SECURITY_ATTRIBUTES& sa, SECURITY_DESCRIPTOR& sd,
                                 std::vector<BYTE>& sid_buf, ACL** out_acl) {
    if (!InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION))
        return false;

    // Get current user SID
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return false;

    DWORD needed = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &needed);
    sid_buf.resize(needed);
    BOOL ok = GetTokenInformation(token, TokenUser, sid_buf.data(), needed, &needed);
    CloseHandle(token);
    if (!ok) return false;

    auto user_sid = reinterpret_cast<PTOKEN_USER>(sid_buf.data())->User.Sid;

    // LocalSystem SID
    PSID system_sid = nullptr;
    if (!ConvertStringSidToSidW(L"S-1-5-18", &system_sid))
        return false;

    EXPLICIT_ACCESSW ea[2] = {};
    ea[0].grfAccessPermissions = GENERIC_READ | GENERIC_WRITE;
    ea[0].grfAccessMode = SET_ACCESS;
    ea[0].grfInheritance = NO_INHERITANCE;
    ea[0].Trustee.TrusteeForm = TRUSTEE_IS_SID;
    ea[0].Trustee.TrusteeType = TRUSTEE_IS_USER;
    ea[0].Trustee.ptstrName = static_cast<LPWSTR>(user_sid);

    ea[1].grfAccessPermissions = GENERIC_READ | GENERIC_WRITE;
    ea[1].grfAccessMode = SET_ACCESS;
    ea[1].grfInheritance = NO_INHERITANCE;
    ea[1].Trustee.TrusteeForm = TRUSTEE_IS_SID;
    ea[1].Trustee.TrusteeType = TRUSTEE_IS_USER;
    ea[1].Trustee.ptstrName = static_cast<LPWSTR>(system_sid);

    ACL* acl = nullptr;
    DWORD acl_err = SetEntriesInAclW(2, ea, nullptr, &acl);
    LocalFree(system_sid);
    if (acl_err != ERROR_SUCCESS) return false;

    if (!SetSecurityDescriptorDacl(&sd, TRUE, acl, FALSE)) {
        LocalFree(acl);
        return false;
    }

    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = &sd;
    sa.bInheritHandle = FALSE;
    *out_acl = acl;
    return true;
}

static std::wstring escape_json(const std::wstring& s) {
    std::wostringstream oss;
    for (wchar_t c : s) {
        switch (c) {
            case L'"':  oss << L"\\\""; break;
            case L'\\': oss << L"\\\\"; break;
            case L'\n': oss << L"\\n";  break;
            case L'\r': oss << L"\\r";  break;
            case L'\t': oss << L"\\t";  break;
            case L'\0': oss << L"\\u0000"; break;
            default:
                if (c < 0x20) {
                    // Escape all control characters
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

static std::wstring handle_list() {
    auto windows = enumerate_windows(true);
    std::wostringstream oss;
    oss << L"{\"ok\":true,\"windows\":[";
    bool first = true;
    for (const auto& w : windows) {
        if (!w.visible) continue;
        int ww = w.rect.right - w.rect.left;
        int wh = w.rect.bottom - w.rect.top;
        if (ww <= 1 && wh <= 1 && !w.minimized) continue;
        if (!first) oss << L",";
        first = false;
        oss << L"{\"hwnd\":\"" << hwnd_hex(w.hwnd)
            << L"\",\"pid\":" << w.pid
            << L",\"title\":\"" << escape_json(w.title)
            << L"\",\"className\":\"" << escape_json(w.className)
            << L"\",\"width\":" << ww
            << L",\"height\":" << wh
            << L",\"minimized\":" << (w.minimized ? L"true" : L"false")
            << L"}";
    }
    oss << L"]}";
    return oss.str();
}

static std::wstring handle_capture(const std::wstring& title, uint64_t hwnd_val,
                                    const std::wstring& out, bool exact,
                                    uint32_t pid, const std::wstring& process,
                                    const std::wstring& className) {
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
        } else if (!filtered.empty()) {
            target = filtered[0];
            found = true;
        }
    }

    if (!found) {
        return L"{\"ok\":false,\"errorCode\":\"WINDOW_NOT_FOUND\",\"message\":\"No window matched\"}";
    }

    // Capture via shared pipeline
    CaptureRequest req;
    req.hwnd = target.hwnd;
    req.title = target.title;
    req.out = out;
    req.timeout_ms = 3000;

    auto cr = capture_to_file(req);
    if (!cr.ok) {
        std::wostringstream oss;
        oss << L"{\"ok\":false,\"errorCode\":\"" << escape_json(cr.error_code)
            << L"\",\"message\":\"" << escape_json(cr.message) << L"\"";
        if (!cr.stage.empty())
            oss << L",\"stage\":\"" << escape_json(cr.stage) << L"\"";
        if (!cr.hresult.empty())
            oss << L",\"hresult\":\"" << escape_json(cr.hresult) << L"\"";
        if (!cr.suggestion.empty())
            oss << L",\"suggestion\":\"" << escape_json(cr.suggestion) << L"\"";
        oss << L"}";
        return oss.str();
    }

    int ww = target.rect.right - target.rect.left;
    int wh = target.rect.bottom - target.rect.top;

    std::wostringstream oss;
    oss << L"{\"ok\":true,\"matchedWindow\":{\"title\":\"" << escape_json(target.title)
        << L"\",\"hwnd\":\"" << hwnd_hex(target.hwnd)
        << L"\",\"pid\":" << target.pid
        << L",\"width\":" << ww << L",\"height\":" << wh
        << L",\"minimized\":" << (target.minimized ? L"true" : L"false")
        << L"},\"screenshotPath\":\"" << escape_json(cr.out_path) << L"\"}";
    return oss.str();
}

// Minimal JSON value extraction (handles escaped quotes and backslashes)
static std::wstring json_get_string(const std::wstring& json, const std::wstring& key) {
    auto pos = json_value_position(json, key);
    if (pos == std::wstring::npos) return L"";
    if (pos >= json.size() || json[pos] != L'"') return L"";
    pos++; // skip opening quote
    // Find closing quote, handling escaped chars
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

static uint64_t json_get_uint64(const std::wstring& json, const std::wstring& key) {
    auto pos = json_value_position(json, key);
    if (pos == std::wstring::npos) return 0;
    uint64_t value = 0;
    return parse_unsigned(std::wstring_view(json_token(json, key)), value) ? value : 0;
}

static bool json_get_bool(const std::wstring& json, const std::wstring& key) {
    auto pos = json_value_position(json, key);
    if (pos == std::wstring::npos) return false;
    return json_token(json, key) == L"true";
}

static std::wstring process_request(const std::wstring& request) {
    auto action = json_get_string(request, L"action");
    if (action == L"ping") {
        // Check if session is interactive
        HWINSTA ws = GetProcessWindowStation();
        bool interactive = false;
        if (ws) {
            USEROBJECTFLAGS flags{};
            DWORD needed = 0;
            if (GetUserObjectInformationW(ws, UOI_FLAGS, &flags, sizeof(flags), &needed))
                interactive = (flags.dwFlags & WSF_VISIBLE) != 0;
        }
        std::wostringstream oss;
        oss << L"{\"ok\":true,\"server\":{\"version\":\"1.7.4\""
            << L",\"userInteractive\":" << (interactive ? L"true" : L"false")
            << L"}}";
        return oss.str();
    } else if (action == L"list") {
        return handle_list();
    } else if (action == L"capture") {
        auto title = json_get_string(request, L"title");
        auto out = json_get_string(request, L"out");
        auto hwnd_val = json_get_uint64(request, L"hwnd");
        bool exact = json_get_bool(request, L"exact");
        uint32_t pid = 0;
        if (json_value_position(request, L"pid") != std::wstring::npos &&
            !parse_u32_strict(json_token(request, L"pid"), pid))
            return L"{\"ok\":false,\"errorCode\":\"BAD_ARGS\",\"message\":\"Invalid PID\"}";
        auto process = json_get_string(request, L"process");
        auto className = json_get_string(request, L"className");
        if (out.empty()) out = L".";
        return handle_capture(title, hwnd_val, out, exact, pid, process, className);
    }
    return L"{\"ok\":false,\"errorCode\":\"BAD_ACTION\",\"message\":\"Unknown action\"}";
}

int run_server(const std::wstring& pipe_name) {
    std::wstring full_pipe = L"\\\\.\\pipe\\" + pipe_name;
    std::wcerr << L"[server] Listening on " << full_pipe << L"\n";
    std::wcerr << L"[server] Agent connects via: wgccli.exe --client <json>\n";
    std::wcerr << L"[server] Press Ctrl+C to stop.\n";

    // Build DACL: current user + LocalSystem only
    SECURITY_ATTRIBUTES sa{};
    SECURITY_DESCRIPTOR sd{};
    std::vector<BYTE> sid_buf;
    ACL* acl = nullptr;
    bool has_dacl = build_pipe_security(sa, sd, sid_buf, &acl);
    if (!has_dacl) {
        std::wcerr << L"[server] Error: failed to build DACL. Refusing to start for security.\n";
        return 1;
    }

    while (true) {
        HANDLE hPipe = CreateNamedPipeW(
            full_pipe.c_str(),
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
            PIPE_UNLIMITED_INSTANCES,
            65536, 65536, 0,
            has_dacl ? &sa : nullptr);

        if (hPipe == INVALID_HANDLE_VALUE) {
            std::wcerr << L"[server] CreateNamedPipe failed: " << GetLastError() << L"\n";
            return 1;
        }

        if (!ConnectNamedPipe(hPipe, nullptr)) {
            if (GetLastError() != ERROR_PIPE_CONNECTED) {
                CloseHandle(hPipe);
                continue;
            }
        }

        // Read request
        wchar_t buf[32768]{};
        DWORD bytes_read = 0;
        BOOL read_ok = ReadFile(hPipe, buf, sizeof(buf) - sizeof(wchar_t), &bytes_read, nullptr);
        if (!read_ok) {
            if (GetLastError() == ERROR_MORE_DATA) {
                // Message too large for buffer
                const wchar_t* err = L"{\"ok\":false,\"errorCode\":\"REQUEST_TOO_LARGE\",\"message\":\"Request exceeds 64KB buffer\"}";
                DWORD err_written = 0;
                WriteFile(hPipe, err, static_cast<DWORD>(wcslen(err) * sizeof(wchar_t)), &err_written, nullptr);
                FlushFileBuffers(hPipe);
            }
            DisconnectNamedPipe(hPipe);
            CloseHandle(hPipe);
            continue;
        }
        if (bytes_read == 0) {
            DisconnectNamedPipe(hPipe);
            CloseHandle(hPipe);
            continue;
        }

        std::wstring request(buf, bytes_read / sizeof(wchar_t));
        std::wstring response = process_request(request);

        // Write response
        DWORD bytes_written = 0;
        WriteFile(hPipe, response.c_str(),
                  static_cast<DWORD>(response.size() * sizeof(wchar_t)),
                  &bytes_written, nullptr);

        FlushFileBuffers(hPipe);
        DisconnectNamedPipe(hPipe);
        CloseHandle(hPipe);
    }

    return 0;
}
