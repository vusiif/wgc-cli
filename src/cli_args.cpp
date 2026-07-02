#include "cli_args.h"
#include <iostream>
#include <algorithm>
#include <cstdlib>

static std::wstring to_lower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), ::towlower);
    return s;
}

// Strict u32 parse: non-empty, fully consumed, no negative, no overflow
static bool parse_u32_strict(const std::wstring& s, uint32_t& out) {
    if (s.empty()) return false;
    if (s[0] == L'-') return false;
    wchar_t* end = nullptr;
    unsigned long val = std::wcstoul(s.c_str(), &end, 10);
    if (end != s.c_str() + s.size()) return false;  // trailing chars
    if (val > UINT32_MAX) return false;
    out = static_cast<uint32_t>(val);
    return true;
}

static uint64_t parse_hwnd_str(const std::wstring& s) {
    if (s.empty()) return 0;
    if (s.size() > 2 && s[0] == L'0' && (s[1] == L'x' || s[1] == L'X')) {
        return std::wcstoull(s.c_str(), nullptr, 16);
    }
    return std::wcstoull(s.c_str(), nullptr, 10);
}

bool parse_args(int argc, wchar_t* argv[], Options& opts) {
    for (int i = 1; i < argc; ++i) {
        std::wstring arg = argv[i];

        if (arg == L"--help" || arg == L"-h") {
            opts.help = true;
        } else if (arg == L"--version") {
            opts.version = true;
        } else if (arg == L"--list") {
            opts.list = true;
        } else if (arg == L"--exact") {
            opts.exact = true;
        } else if (arg == L"--json") {
            opts.json = true;
        } else if (arg == L"--include-minimized") {
            opts.include_minimized = true;
        } else if (arg == L"--no-restore") {
            opts.restore = false;
        } else if (arg == L"--require-unique") {
            opts.require_unique = true;
        } else if (arg == L"--doctor") {
            opts.doctor = true;
        } else if (arg == L"--health") {
            opts.health = true;
        } else if (arg == L"--mcp-stdio") {
            opts.mcp_stdio = true;
        } else if (arg == L"--pid") {
            if (++i >= argc) {
                std::wcerr << L"Error: --pid requires a number\n";
                return false;
            }
            if (!parse_u32_strict(argv[i], opts.pid)) {
                std::wcerr << L"Error: --pid requires a valid number, got: " << argv[i] << L"\n";
                return false;
            }
            opts.has_pid = true;
        } else if (arg == L"--process") {
            if (++i >= argc) {
                std::wcerr << L"Error: --process requires an exe name\n";
                return false;
            }
            opts.process = argv[i];
            opts.has_process = true;
        } else if (arg == L"--class-name") {
            if (++i >= argc) {
                std::wcerr << L"Error: --class-name requires a name\n";
                return false;
            }
            opts.className = argv[i];
            opts.has_className = true;
        } else if (arg == L"--delay-ms") {
            if (++i >= argc) {
                std::wcerr << L"Error: --delay-ms requires a number\n";
                return false;
            }
            if (!parse_u32_strict(argv[i], opts.delay_ms)) {
                std::wcerr << L"Error: --delay-ms requires a valid number, got: " << argv[i] << L"\n";
                return false;
            }
        } else if (arg == L"--max-width") {
            if (++i >= argc) {
                std::wcerr << L"Error: --max-width requires a number\n";
                return false;
            }
            if (!parse_u32_strict(argv[i], opts.max_width)) {
                std::wcerr << L"Error: --max-width requires a valid number, got: " << argv[i] << L"\n";
                return false;
            }
        } else if (arg == L"--resize") {
            if (++i >= argc) {
                std::wcerr << L"Error: --resize requires WxH (e.g. 1024x768)\n";
                return false;
            }
            std::wstring s = argv[i];
            auto xpos = s.find(L'x');
            if (xpos == std::wstring::npos) xpos = s.find(L'X');
            if (xpos == std::wstring::npos || xpos == 0 || xpos == s.size() - 1) {
                std::wcerr << L"Error: --resize format is WxH (e.g. 1024x768)\n";
                return false;
            }
            if (!parse_u32_strict(s.substr(0, xpos), opts.resize_w) ||
                !parse_u32_strict(s.substr(xpos + 1), opts.resize_h)) {
                std::wcerr << L"Error: --resize requires positive integers in WxH format, got: " << s << L"\n";
                return false;
            }
        } else if (arg == L"--format") {
            if (++i >= argc) {
                std::wcerr << L"Error: --format requires png, jpg, jpeg, or bmp\n";
                return false;
            }
            opts.format = argv[i];
            {
                std::wstring fmt_lower = to_lower(opts.format);
                if (fmt_lower != L"png" && fmt_lower != L"jpg" &&
                    fmt_lower != L"jpeg" && fmt_lower != L"bmp") {
                    std::wcerr << L"Error: --format must be png, jpg, jpeg, or bmp, got: " << argv[i] << L"\n";
                    return false;
                }
            }
        } else if (arg == L"--crop") {
            if (++i >= argc) {
                std::wcerr << L"Error: --crop requires x,y,w,h\n";
                return false;
            }
            std::wstring s = argv[i];
            // Parse x,y,w,h with strict validation
            {
                const wchar_t* p = s.c_str();
                wchar_t* end = nullptr;
                uint32_t parts[4] = {};
                for (int j = 0; j < 4; ++j) {
                    if (*p == L'\0') {
                        std::wcerr << L"Error: --crop requires 4 values (x,y,w,h), got: " << s << L"\n";
                        return false;
                    }
                    unsigned long val = std::wcstoul(p, &end, 10);
                    if (end == p || val > UINT32_MAX) {
                        std::wcerr << L"Error: --crop value " << (j+1) << " is invalid in: " << s << L"\n";
                        return false;
                    }
                    parts[j] = static_cast<uint32_t>(val);
                    p = end;
                    if (j < 3) {
                        if (*p != L',') {
                            std::wcerr << L"Error: --crop format is x,y,w,h, got: " << s << L"\n";
                            return false;
                        }
                        p++; // skip comma
                    }
                }
                if (*p != L'\0') {
                    std::wcerr << L"Error: --crop trailing characters in: " << s << L"\n";
                    return false;
                }
                opts.crop_x = parts[0];
                opts.crop_y = parts[1];
                opts.crop_w = parts[2];
                opts.crop_h = parts[3];
            }
            opts.has_crop = true;
        } else if (arg == L"--restore") {
            opts.restore = true;
        } else if (arg == L"--title") {
            if (++i >= argc) {
                std::wcerr << L"Error: --title requires an argument\n";
                return false;
            }
            opts.title = argv[i];
            opts.has_title = true;
        } else if (arg == L"--hwnd") {
            if (++i >= argc) {
                std::wcerr << L"Error: --hwnd requires an argument\n";
                return false;
            }
            opts.hwnd = parse_hwnd_str(argv[i]);
            opts.has_hwnd = true;
            if (opts.hwnd == 0) {
                std::wcerr << L"Error: invalid HWND value: " << argv[i] << L"\n";
                return false;
            }
        } else if (arg == L"--out") {
            if (++i >= argc) {
                std::wcerr << L"Error: --out requires an argument\n";
                return false;
            }
            opts.out = argv[i];
        } else if (arg == L"--timeout-ms") {
            if (++i >= argc) {
                std::wcerr << L"Error: --timeout-ms requires an argument\n";
                return false;
            }
            if (!parse_u32_strict(argv[i], opts.timeout_ms)) {
                std::wcerr << L"Error: --timeout-ms requires a valid number, got: " << argv[i] << L"\n";
                return false;
            }
        } else if (arg == L"--server") {
            opts.server = true;
        } else if (arg == L"--client") {
            if (++i >= argc) {
                std::wcerr << L"Error: --client requires a JSON argument\n";
                return false;
            }
            opts.client = argv[i];
        } else if (arg == L"--pipe") {
            if (++i >= argc) {
                std::wcerr << L"Error: --pipe requires a name\n";
                return false;
            }
            opts.pipe = argv[i];
        } else {
            std::wcerr << L"Error: unknown argument: " << arg << L"\n";
            return false;
        }
    }

    // Validate mutually exclusive args
    if (opts.has_title && opts.has_hwnd) {
        std::wcerr << L"Error: --title and --hwnd are mutually exclusive\n";
        return false;
    }

    return true;
}

void print_help() {
    std::wcout <<
        L"wgccli - Windows Graphics Capture command-line tool\n"
        L"\n"
        L"Usage:\n"
        L"  wgccli.exe --title <text> [--exact] --out <dir-or-file> [options]\n"
        L"  wgccli.exe --hwnd <hex-or-decimal> --out <dir-or-file> [options]\n"
        L"  wgccli.exe --list\n"
        L"  wgccli.exe --server [--pipe <name>]\n"
        L"  wgccli.exe --client <json-request> [--pipe <name>]\n"
        L"\n"
        L"Required (one of):\n"
        L"  --title <text>             Match window by title (partial, case-insensitive)\n"
        L"  --hwnd <hex-or-decimal>    Capture by window handle\n"
        L"  --list                     List candidate windows\n"
        L"  --server                   Start named pipe server (for agent use)\n"
        L"  --client <json>            Send request to server\n"
        L"\n"
        L"Options:\n"
        L"  --exact                    Exact title match (case-insensitive)\n"
        L"  --out <dir-or-file>        Output directory or full PNG file path\n"
        L"  --json                     JSON output\n"
        L"  --timeout-ms <number>      Wait for first frame timeout (default: 3000)\n"
        L"  --include-minimized        Include minimized windows in list (default: true)\n"
        L"  --no-restore               Do not try to restore minimized window\n"
        L"  --restore                  Try to restore window before capture (default: true)\n"
        L"  --pid <pid>                Filter by process ID\n"
        L"  --process <exe-name>       Filter by process name (e.g. chrome.exe)\n"
        L"  --class-name <name>        Filter by window class name\n"
        L"  --delay-ms <number>        Wait before capture (default: 0)\n"
        L"  --max-width <number>       Scale down to max width (proportional)\n"
        L"  --resize <WxH>             Resize to exact dimensions (e.g. 1024x768)\n"
        L"  --format <png|jpg|jpeg|bmp> Output format (default: png)\n"
        L"  --crop <x,y,w,h>           Crop region after capture\n"
        L"  --require-unique           Fail if multiple windows match (use --list --json first)\n"
        L"  --doctor                   Run environment diagnostics\n"
        L"  --health                   Ping running server and exit\n"
        L"  --mcp-stdio                Start MCP stdio server (JSON-RPC over stdin/stdout)\n"
        L"  --pipe <name>              Named pipe name (default: wgccli)\n"
        L"  --help                     Print this help\n"
        L"  --version                  Print version\n"
        L"\n"
        L"Server/Client mode (for AI agents):\n"
        L"  # Start server in user's interactive session:\n"
        L"  wgccli.exe --server\n"
        L"\n"
        L"  # Agent sends requests via pipe:\n"
        L"  wgccli.exe --client \"{\\\"action\\\":\\\"list\\\"}\"\n"
        L"  wgccli.exe --client \"{\\\"action\\\":\\\"capture\\\",\\\"title\\\":\\\"Notepad\\\",\\\"out\\\":\\\"C:\\\\Temp\\\"}\"\n"
        L"\n"
        L"Examples:\n"
        L"  wgccli.exe --title \"Notepad\" --out C:\\Temp\n"
        L"  wgccli.exe --title \"Android Studio\" --exact --out C:\\Screenshots\n"
        L"  wgccli.exe --hwnd 0x00123456 --out C:\\Screenshots --json\n"
        L"  wgccli.exe --list\n";
}

void print_version() {
    std::wcout << L"wgccli 1.7.0\n";
}
