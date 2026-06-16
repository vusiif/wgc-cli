---
name: wgc-screenshot
description: Capture screenshots of Windows GUI application windows using wgccli. Use this when an agent needs to inspect a Windows app, browser, IDE, emulator, installer, dialog, or any visible desktop window without relying on active-window focus, the clipboard, or manual screenshot tools.
---

# WGC Screenshot Skill

Use this skill when you need to visually inspect a Windows GUI window from an agent workflow.

`wgccli.exe` captures Windows application windows by title or HWND using Windows.Graphics.Capture. It is especially useful when an agent needs screenshots of apps such as Android Studio, Chrome, Notepad, emulators, installers, or desktop tools.

## When to use

Use this skill when:

* You need a screenshot of a specific Windows GUI window.
* You need to inspect the current visual state of an app.
* You need to capture a window without activating it.
* You need a deterministic screenshot path for later image analysis.
* The agent is running in a Windows environment and `wgccli.exe` is available.

Do not use this skill when:

* The task does not require visual inspection.
* The target is a web page that can be inspected more directly through browser automation or DOM access.
* The target window is known to be protected, DRM-rendered, on the secure desktop, or inaccessible from the current Windows session.
* The operating system is not Windows.

## Prerequisites

Before using the tool, check that `wgccli.exe` is available.

Preferred checks:

```powershell
where.exe wgccli.exe
```

or:

```powershell
Get-Command wgccli.exe -ErrorAction SilentlyContinue
```

If it is not in `PATH`, look for a known local path such as:

```powershell
$env:WGC_CLI_PATH
```

or ask the user where `wgccli.exe` is installed.

Create a screenshot output directory before capture:

```powershell
$OutDir = Join-Path $env:TEMP "agent-screenshots"
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
```

## Basic workflow

### 1. List visible windows

When the target title is uncertain, first list visible windows:

```powershell
wgccli.exe --list
```

Use the output to identify a stable title or HWND.

Prefer HWND capture when multiple windows have similar titles.

### 2. Capture by partial title

Use partial title matching for common cases:

```powershell
wgccli.exe --title "Chrome" --out "$env:TEMP\agent-screenshots" --json
```

### 3. Capture by exact title

Use exact title matching when ambiguity matters:

```powershell
wgccli.exe --title "Android Studio - MyProject" --exact --out "$env:TEMP\agent-screenshots" --json
```

### 4. Capture by HWND

Use HWND when available from `--list` or previous output:

```powershell
wgccli.exe --hwnd 0x0000000000123456 --out "$env:TEMP\agent-screenshots" --json
```

### 5. Parse JSON output

Always prefer `--json` for agent workflows.

Expected successful shape:

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

After capture:

1. Confirm `ok` is true.
2. Read `screenshotPath`.
3. Verify the file exists.
4. Use the agent’s image-viewing capability to inspect the PNG.

PowerShell example:

```powershell
$json = wgccli.exe --title "Chrome" --out "$env:TEMP\agent-screenshots" --json | ConvertFrom-Json

if (-not $json.ok) {
    throw "wgccli capture failed"
}

if (-not (Test-Path $json.screenshotPath)) {
    throw "Screenshot file was not created: $($json.screenshotPath)"
}

$json.screenshotPath
```

## Server/client mode for agent session isolation

Use server/client mode when the agent runs in a different Windows session from the interactive desktop, such as SSH, service execution, scheduled jobs, or remote automation.

### Start server in the user’s interactive desktop session

This must be started where the GUI windows are accessible:

```powershell
wgccli.exe --server
```

Optionally use a custom pipe name:

```powershell
wgccli.exe --server --pipe my-agent-wgc
```

### Send requests from the agent session

List windows:

```powershell
wgccli.exe --client "{\"action\":\"list\"}"
```

Capture by title:

```powershell
wgccli.exe --client "{\"action\":\"capture\",\"title\":\"Chrome\",\"out\":\"C:\\Temp\\agent-screenshots\"}"
```

Capture by HWND:

```powershell
wgccli.exe --client "{\"action\":\"capture\",\"hwnd\":12345,\"out\":\"C:\\Temp\\agent-screenshots\"}"
```

Use a custom pipe name if the server was started with one:

```powershell
wgccli.exe --pipe my-agent-wgc --client "{\"action\":\"list\"}"
```

## Recommended agent procedure

Follow this order:

1. Check that the environment is Windows.
2. Check that `wgccli.exe` exists.
3. Create an output directory.
4. If the target window is ambiguous, run `--list`.
5. Prefer exact title or HWND when possible.
6. Run capture with `--json`.
7. Parse `screenshotPath`.
8. Verify the PNG exists.
9. Inspect the image.
10. If capture fails, use the troubleshooting table below.

## Title matching guidance

When matching by title:

1. Prefer exact match for known full titles.
2. Use partial match for broad app names.
3. If multiple windows match, run `--list` and capture by HWND.
4. Avoid overly generic titles such as `"Chrome"`, `"Settings"`, or `"Untitled"` when precision matters.
5. If a minimized window fails, retry after restoring it manually or without relying on minimized capture.

## Troubleshooting

### Window not found

Symptoms:

* Exit code `3`
* No matching title
* Empty or unexpected `--list`

Actions:

```powershell
wgccli.exe --list
```

Then retry with:

* A shorter title substring.
* `--exact` removed.
* HWND capture.
* Server/client mode if the agent may be in a different session.

### WGC unavailable

Symptoms:

* Exit code `4`

Actions:

* Confirm Windows 10 version 1809 or later.
* Confirm a D3D11-capable GPU is available.
* Confirm the app is running in the interactive desktop session.

### Capture failed

Symptoms:

* Exit code `5`
* Window exists but PNG is not saved.

Actions:

* Check whether the window is protected, elevated, DRM-rendered, or on a secure desktop.
* Try running the agent or server with matching privileges.
* Try server/client mode from the interactive user session.
* Try another window to confirm the tool works.

### Save PNG failed

Symptoms:

* Exit code `6`

Actions:

* Confirm the output directory exists.
* Confirm write permissions.
* Try a simple path such as:

```powershell
C:\Temp\agent-screenshots
```

### Timeout

Symptoms:

* Exit code `7`

Actions:

* Increase timeout:

```powershell
wgccli.exe --title "Chrome" --out "C:\Temp\agent-screenshots" --json --timeout-ms 8000
```

* Confirm the target window is visible and responsive.

### Permission denied

Symptoms:

* Exit code `8`

Actions:

* Check whether the target window is elevated.
* Run the capture process with matching privileges.
* Avoid protected system windows and secure desktop prompts.

## Exit codes

| Code | Meaning                                |
| ---: | -------------------------------------- |
|    0 | Success                                |
|    1 | General error                          |
|    2 | Bad arguments                          |
|    3 | Window not found                       |
|    4 | Windows.Graphics.Capture not available |
|    5 | Capture failed                         |
|    6 | Save PNG failed                        |
|    7 | Timeout                                |
|    8 | Permission denied                      |

## Command reference

List visible windows:

```powershell
wgccli.exe --list
```

Capture by title:

```powershell
wgccli.exe --title "Notepad" --out "C:\Temp\agent-screenshots" --json
```

Capture by exact title:

```powershell
wgccli.exe --title "Android Studio - MyProject" --exact --out "C:\Temp\agent-screenshots" --json
```

Capture by HWND:

```powershell
wgccli.exe --hwnd 0x0000000000123456 --out "C:\Temp\agent-screenshots" --json
```

Capture with longer timeout:

```powershell
wgccli.exe --title "Chrome" --out "C:\Temp\agent-screenshots" --json --timeout-ms 8000
```

Start server:

```powershell
wgccli.exe --server
```

Client list request:

```powershell
wgccli.exe --client "{\"action\":\"list\"}"
```

Client capture request:

```powershell
wgccli.exe --client "{\"action\":\"capture\",\"title\":\"Notepad\",\"out\":\"C:\\Temp\\agent-screenshots\"}"
```

## Safety and reliability rules

* Prefer `--json` so the agent can parse results reliably.
* Never assume the active window is the target window.
* Never rely on the clipboard.
* Do not scan arbitrary screenshot folders looking for the newest file; use the returned `screenshotPath`.
* Do not repeatedly capture in a tight loop unless the task explicitly requires it.
* Be careful with screenshots that may contain secrets, credentials, personal data, or private messages.
* If the screenshot may contain sensitive information, mention that visual inspection can expose on-screen data.
* If capture fails because the window is protected or inaccessible, explain the limitation instead of bypassing platform security.

## Minimal robust PowerShell helper

Use this helper when you need a repeatable capture routine:

```powershell
param(
    [Parameter(Mandatory=$true)]
    [string]$Title,

    [string]$OutDir = "$env:TEMP\agent-screenshots",

    [switch]$Exact
)

$ErrorActionPreference = "Stop"

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$cmd = @("wgccli.exe", "--title", $Title, "--out", $OutDir, "--json")

if ($Exact) {
    $cmd += "--exact"
}

$output = & $cmd[0] $cmd[1..($cmd.Length - 1)]

if ($LASTEXITCODE -ne 0) {
    throw "wgccli failed with exit code $LASTEXITCODE. Output: $output"
}

$json = $output | ConvertFrom-Json

if (-not $json.ok) {
    throw "wgccli returned ok=false. Output: $output"
}

if (-not (Test-Path $json.screenshotPath)) {
    throw "Screenshot path does not exist: $($json.screenshotPath)"
}

Write-Output $json.screenshotPath
```

## Example agent reasoning pattern

When asked to inspect a Windows app:

1. “I need a visual screenshot of the target app.”
2. “I will list windows if the exact title is unknown.”
3. “I will capture using `wgccli.exe --json`.”
4. “I will inspect the returned PNG path.”
5. “I will report what is visible, and mention uncertainty if the screenshot is unclear.”

## Limitations

* Minimized windows may not be capturable.
* Some protected, elevated, DRM-rendered, secure desktop, or cross-session windows may fail.
* Cursor capture is not included.
* Windows from another session or desktop require server/client mode or may be inaccessible.
