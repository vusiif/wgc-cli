# wgccli

Windows command-line screenshot tool using **Windows.Graphics.Capture** API. Designed for AI/Agent workflows — capture any window by title or HWND, without relying on active window focus or clipboard.

## Features

- Capture windows by title (partial/exact match, case-insensitive)
- Capture windows by HWND
- List all visible windows with `--list`
- PNG output with auto-generated filenames
- JSON output mode for programmatic use
- Minimized window auto-restore before capture
- **Server/Client mode** — named pipe IPC for AI agents running in different sessions
- No GUI, no clipboard, no disk scanning

## Download

Grab `wgccli.exe` from [Releases](https://github.com/vusiif/wgc-cli/releases).

## AI Agent Skill

This repo includes a [SKILL.md](SKILL.md) for Claude Code and other AI agents. It provides step-by-step instructions for capturing Windows GUI screenshots, including server/client mode for session isolation, troubleshooting, and a ready-to-use PowerShell helper.

## Usage

```bash
# Capture by window title (partial match)
wgccli.exe --title "Notepad" --out C:\Screenshots

# Capture by exact title
wgccli.exe --title "Android Studio - MyProject" --exact --out C:\Screenshots

# Capture by HWND
wgccli.exe --hwnd 0x0000000000123456 --out C:\Screenshots

# List visible windows
wgccli.exe --list

# JSON output
wgccli.exe --title "Chrome" --out C:\Screenshots --json
```

### Server/Client Mode (for AI Agents)

When an AI agent runs in a different session (e.g., as a service or SSH), it cannot directly enumerate or capture GUI windows due to session isolation. The server/client mode solves this by using named pipes for IPC.

```bash
# 1. Start the server in the user's interactive session (e.g., via Task Scheduler or startup script)
wgccli.exe --server

# 2. Agent sends requests via the named pipe
wgccli.exe --client "{\"action\":\"list\"}"
wgccli.exe --client "{\"action\":\"capture\",\"title\":\"Notepad\",\"out\":\"C:\\Temp\"}"
wgccli.exe --client "{\"action\":\"capture\",\"hwnd\":12345,\"out\":\"C:\\Temp\"}"
```

The server runs in the user's desktop session where it has access to GUI windows. The client can run from any session (agent, service, SSH) and communicates over `\\.\pipe\wgccli`.

Use `--pipe <name>` to customize the pipe name (default: `wgccli`).

### Output (plain mode)

```
Matched window: Android Studio - MyProject
Handle: 0x0000000000123456
Window size: 1920x1080
RESULT_SCREENSHOT_PATH=C:\Screenshots\20260523-153022-android-studio.png
```

### Output (JSON mode)

```json
{
  "ok": true,
  "matchedWindow": {
    "title": "Android Studio - MyProject",
    "hwnd": "0x0000000000123456",
    "pid": 12345,
    "width": 1920,
    "height": 1080,
    "state": "normal"
  },
  "screenshotPath": "C:\\Screenshots\\20260523-153022-android-studio.png"
}
```

## Parameters

| Parameter | Description |
|---|---|
| `--title <text>` | Match window by title (partial, case-insensitive) |
| `--exact` | Exact title match |
| `--hwnd <hex>` | Capture by window handle |
| `--list` | List candidate windows |
| `--out <dir-or-file>` | Output directory or full PNG path |
| `--json` | JSON output |
| `--timeout-ms <n>` | Wait for first frame (default: 3000) |
| `--no-restore` | Don't restore minimized windows |
| `--server` | Start named pipe server (for agent use) |
| `--client <json>` | Send request to server via pipe |
| `--pipe <name>` | Custom pipe name (default: `wgccli`) |
| `--help` | Print help |
| `--version` | Print version |

## Title Matching Priority

1. Exact match (case-insensitive)
2. Starts with (case-insensitive)
3. Contains (case-insensitive)
4. Shorter title preferred

## Exit Codes

| Code | Meaning |
|---|---|
| 0 | Success |
| 1 | General error |
| 2 | Bad arguments |
| 3 | Window not found |
| 4 | WGC not available |
| 5 | Capture failed |
| 6 | Save PNG failed |
| 7 | Timeout |
| 8 | Permission denied |

## Requirements

- Windows 10 version 1809 (build 17763) or later
- D3D11 capable GPU
- MSVC toolchain (for building from source)

## Build from Source

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Or open in CLion with MSVC toolchain configured.

## Limitations

- Minimized windows may not be capturable (auto-restore is attempted)
- Some protected windows, elevated windows, secure desktop, or DRM surfaces may fail
- Does not capture the cursor
- Windows from another session/desktop are not accessible

## Use Case: AI/Agent Screenshot

This tool is designed for scenarios where a CLI agent (like Claude Code) needs to capture a Windows GUI application:

```bash
# Direct mode (agent in same session)
wgccli.exe --title "Android Studio" --out "$env:USERPROFILE\Pictures\Screenshots"

# Server/Client mode (agent in different session)
# Start server in user session first, then:
wgccli.exe --client "{\"action\":\"capture\",\"title\":\"Android Studio\",\"out\":\"C:\\Screenshots\"}"
```

Unlike ShareX or other screenshot tools, wgccli does not depend on the active window, does not use the clipboard, and does not require scanning output folders.
