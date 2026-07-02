# wgccli

[English](README.md)

Windows 命令行截图工具，基于 **Windows.Graphics.Capture** API。专为 AI/Agent 工作流设计——按窗口标题、HWND、进程名或 PID 捕获任意窗口，不依赖前台焦点或剪贴板。

## 功能特性

- 按窗口标题截图（模糊/精确匹配，不区分大小写）
- 按 HWND、PID、进程名、窗口类名截图
- 以 JSON 格式列出所有可见窗口（`--list --json`）
- 支持 PNG/JPEG/BMP 输出，自动生成文件名
- 图片缩放、裁剪、格式转换
- 最小化窗口自动恢复后截图
- **Server/Client 模式** — 命名管道 IPC，支持跨会话的 AI Agent 调用
- **环境诊断**（`--doctor`）
- 无 GUI、无剪贴板、无磁盘扫描

## 下载

从 [Releases](https://github.com/vusiif/wgc-cli/releases) 下载 `wgccli.exe`。

## AI Agent Skill

本仓库包含 [SKILL.md](SKILL.md)，为 Claude Code 和其他 AI Agent 提供截图指导，包括 server/client 模式、排错指南和可直接使用的 PowerShell 辅助脚本。

## 快速上手

```bash
# 按窗口标题截图
wgccli.exe --title "Notepad" --out C:\Screenshots

# 按 PID 截图
wgccli.exe --pid 12345 --out C:\Screenshots --json

# 按进程名 + 标题组合截图
wgccli.exe --process chrome.exe --title "ChatGPT" --out C:\Screenshots --json

# 以 JSON 列出所有窗口
wgccli.exe --list --json

# 缩放到最大宽度 1280px（节省 VLM token）
wgccli.exe --title "Chrome" --out C:\Screenshots --max-width 1280 --format jpg --json
```

## Server/Client 模式（AI Agent 跨会话截图）

当 AI Agent 运行在不同会话（如服务、SSH）时，无法直接枚举或捕获 GUI 窗口。Server/Client 模式通过命名管道解决此问题。

```bash
# 1. 在用户交互会话中启动 server
wgccli.exe --server

# 2. Agent 通过管道发送请求
wgccli.exe --client "{\"action\":\"list\"}"
wgccli.exe --client "{\"action\":\"capture\",\"title\":\"Notepad\",\"out\":\"C:\\Temp\"}"
wgccli.exe --client "{\"action\":\"capture\",\"process\":\"chrome.exe\",\"out\":\"C:\\Temp\"}"
```

使用 `--pipe <name>` 自定义管道名称（默认：`wgccli`）。

## 参数说明

### 选择器（截图时至少指定一个）

| 参数 | 说明 |
|---|---|
| `--title <文本>` | 按窗口标题匹配（模糊，不区分大小写） |
| `--hwnd <十六进制>` | 按窗口句柄截图 |
| `--pid <pid>` | 按进程 ID 过滤/截图 |
| `--process <exe名>` | 按进程名过滤/截图（如 `chrome.exe`） |
| `--class-name <类名>` | 按窗口类名过滤/截图 |
| `--list` | 列出候选窗口 |
| `--exact` | 精确标题匹配 |

### 输出选项

| 参数 | 说明 |
|---|---|
| `--out <目录或文件>` | 输出目录或完整文件路径 |
| `--json` | JSON 格式输出 |
| `--format <png\|jpg\|jpeg\|bmp>` | 输出格式（默认：png） |

### 图像处理

| 参数 | 说明 |
|---|---|
| `--max-width <数值>` | 等比缩放到最大宽度 |
| `--resize <宽x高>` | 缩放到指定尺寸（如 `1024x768`） |
| `--crop <x,y,宽,高>` | 截图后裁剪指定区域 |

### 行为选项

| 参数 | 说明 |
|---|---|
| `--timeout-ms <毫秒>` | 等待首帧超时（默认：3000） |
| `--delay-ms <毫秒>` | 截图前等待 |
| `--no-restore` | 不恢复最小化窗口 |
| `--require-unique` | 多窗口匹配时直接失败 |

### Server/Client 与诊断

| 参数 | 说明 |
|---|---|
| `--server` | 启动命名管道服务器 |
| `--client <json>` | 通过管道发送请求 |
| `--pipe <名称>` | 自定义管道名（默认：`wgccli`） |
| `--doctor` | 运行环境诊断 |
| `--help` | 打印帮助 |
| `--version` | 打印版本 |

## JSON 输出示例

### 截图成功

```json
{
  "ok": true,
  "matchedWindow": {
    "title": "Android Studio",
    "hwnd": "0x0000000000123456",
    "pid": 12345,
    "width": 1920,
    "height": 1080,
    "className": "SunAwtFrame",
    "state": "normal"
  },
  "candidates": [...],
  "screenshotPath": "C:\\Screenshots\\20260619-153022-android-studio.png"
}
```

### 错误诊断

```json
{
  "ok": false,
  "errorCode": "CAPTURE_FAILED",
  "exitCode": 5,
  "stage": "CreateForWindow",
  "hresult": "0x80070005",
  "message": "Access denied",
  "suggestion": "Try matching elevation or use server/client mode."
}
```

### 环境诊断

```json
{
  "ok": true,
  "osBuild": 26100,
  "wgcAvailable": true,
  "d3d11Available": true,
  "interactiveSession": true,
  "elevated": false,
  "windowsCount": 12
}
```

## 标题匹配优先级

1. 完全相等（不区分大小写）
2. 开头匹配（不区分大小写）
3. 包含匹配（不区分大小写）
4. 标题更短者优先

## 退出码

| 代码 | 含义 |
|---|---|
| 0 | 成功 |
| 1 | 通用错误 |
| 2 | 参数错误 |
| 3 | 窗口未找到 / 匹配歧义 |
| 4 | WGC 不可用 |
| 5 | 截图失败 |
| 6 | 保存失败 |
| 7 | 超时 |
| 8 | 权限不足 |

## 系统要求

- Windows 10 版本 1903（build 18362）或更高
- 支持 D3D11 的 GPU
- 运行 `wgccli.exe --doctor` 检查兼容性

## 从源码构建

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

或在 CLion 中配置 MSVC 工具链后直接打开。

## 限制

- 最小化窗口可能无法截图（会尝试自动恢复）
- 部分受保护窗口、提权窗口、安全桌面或 DRM 表面可能失败
- 不捕获鼠标光标
- 跨会话/桌面的窗口需要使用 server/client 模式
