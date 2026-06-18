#include "cli_args.h"
#include <iostream>
#include <algorithm>
#include <cstdlib>

static std::wstring to_lower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), ::towlower);
    return s;
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
        } else if (arg == L"--pid") {
            if (++i >= argc) {
                std::wcerr << L"Error: --pid requires a number\n";
                return false;
            }
            opts.pid = static_cast<uint32_t>(std::wcstoul(argv[i], nullptr, 10));
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
            opts.delay_ms = static_cast<uint32_t>(std::wcstoul(argv[i], nullptr, 10));
        } else if (arg == L"--max-width") {
            if (++i >= argc) {
                std::wcerr << L"Error: --max-width requires a number\n";
                return false;
            }
            opts.max_width = static_cast<uint32_t>(std::wcstoul(argv[i], nullptr, 10));
        } else if (arg == L"--resize") {
            if (++i >= argc) {
                std::wcerr << L"Error: --resize requires WxH (e.g. 1024x768)\n";
                return false;
            }
            std::wstring s = argv[i];
            auto xpos = s.find(L'x');
            if (xpos == std::wstring::npos) {
                std::wcerr << L"Error: --resize format is WxH (e.g. 1024x768)\n";
                return false;
            }
            opts.resize_w = static_cast<uint32_t>(std::wcstoul(s.c_str(), nullptr, 10));
            opts.resize_h = static_cast<uint32_t>(std::wcstoul(s.c_str() + xpos + 1, nullptr, 10));
        } else if (arg == L"--format") {
            if (++i >= argc) {
                std::wcerr << L"Error: --format requires png, jpg, or bmp\n";
                return false;
            }
            opts.format = argv[i];
        } else if (arg == L"--crop") {
            if (++i >= argc) {
                std::wcerr << L"Error: --crop requires x,y,w,h\n";
                return false;
            }
            std::wstring s = argv[i];
            wchar_t* end;
            opts.crop_x = static_cast<uint32_t>(std::wcstoul(s.c_str(), &end, 10));
            opts.crop_y = static_cast<uint32_t>(std::wcstoul(end + 1, &end, 10));
            opts.crop_w = static_cast<uint32_t>(std::wcstoul(end + 1, &end, 10));
            opts.crop_h = static_cast<uint32_t>(std::wcstoul(end + 1, nullptr, 10));
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
            opts.timeout_ms = static_cast<uint32_t>(std::wcstoul(argv[i], nullptr, 10));
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
        L"  --format <png|jpg|bmp>     Output format (default: png)\n"
        L"  --crop <x,y,w,h>           Crop region after capture\n"
        L"  --require-unique           Fail if multiple windows match (use --list --json first)\n"
        L"  --doctor                   Run environment diagnostics\n"
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
    std::wcout << L"wgccli 1.4.0\n";
}
