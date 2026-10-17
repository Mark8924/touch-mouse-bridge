# TouchMouse 0.3.0 Preview — Touchscreen click compatibility

A portable Windows utility for games that miss touchscreen taps or require two taps for one click.

- Native ARM64 and x64 executables; no installer, .NET runtime, driver or service.
- Configurable 20–250 ms minimum press duration (default 60 ms).
- Single-click and optional left-double-click modes; touch-promoted right clicks remain single.
- Per-EXE profiles or global mode, notification-area controls and optional startup.
- Minimize hides to tray; closing the settings window exits.

Download the ZIP for your Windows architecture, extract it to a permanent folder and run **TouchMouse.exe**. Settings UI is currently Chinese; the bilingual README explains the controls. Standalone EXEs are also provided. `SHA256SUMS.txt` covers all four downloads.

**Preview:** ARM64 has real-device validation; x64 is compiled but not yet runtime tested. Compatibility varies by game. Binaries are unsigned. Physical mouse and pen events pass through. No networking or game-file changes; no game profiles or user data are included.

Validation: 22 portable engine cases, 10 Windows input checks, 6 tray lifecycle checks, and real touchscreen left-click / touch-and-hold checks on ARM64.

---

# TouchMouse 0.3.0 预览版 — 游戏触屏点击兼容

为触屏点按被漏读、需要点两次才响应一次的游戏提供兼容处理。

- 原生 ARM64 / x64，无需安装器、.NET、服务或驱动。
- 按键保持时间可调至 20–250 毫秒，默认 60 毫秒。
- 标准单击及可选左键双击模式；触屏长按产生的右键始终只有一次。
- 可按 EXE 或全局启用，支持托盘控制和可选自启。
- 最小化隐藏至托盘，点窗口关闭键直接退出。

下载对应架构 ZIP，解压到固定目录后双击 **TouchMouse.exe**，也可下载单独 EXE。校验值见 `SHA256SUMS.txt`。界面目前为中文，仓库提供英文在前、中文在后的说明。

**预览状态：**ARM64 已进行实机验证；x64 已编译，尚未实机验证。不保证适配所有游戏。程序未签名，不联网、不修改游戏文件，不附带游戏配置或用户数据。物理鼠标和触控笔保持原样。
