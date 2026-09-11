# Box0 Voice Link（MicYou 原生插件）

把 BOX0 无线麦的按键事件转成 PC 桌面快捷键。只要 MicYou 在运行，联动就在——不需要独立进程。

## 工作链路

```
BOX0（无线麦页面）
  M 短按开始/停止、M 长按按住说话、右键发送、左键删除
    -> UDP 9125 发送 VOICE_START / VOICE_STOP / VOICE_HOLD_START / VOICE_HOLD_STOP / VOICE_ENTER / VOICE_DELETE
MicYou 插件（本目录编译出的 box0_voice_link.dll）
    -> SendInput 点按 Ctrl+Win+Shift / 按住·松开 左Alt+左Win / Enter / Backspace
微信输入法语音输入（点按模式：Ctrl+Win+Shift；按住说话：左Alt+左Win）
```

固件侧发信号代码：`main/boards/alientek/atk-dnesp32s3-box0/atk_dnesp32s3_box0.cc` 的 `MicSignalPc()`（目标 IP 来自 mDNS 发现 MicYou 的结果，固件端常量 `kMicVoicePort = 9125`）。

## 构建

需要 MinGW-w64 g++（推荐 winlibs 绿色包：https://github.com/brechtsanders/winlibs_mingw/releases 下载 x86_64-posix-seh-ucrt 压缩包解压即可，无需安装）：

```bat
set MINGW_DIR=D:/Opts/winlibs/mingw64    :: 含 bin/g++.exe 的目录
build.bat
```

产物 `box0_voice_link.dll`（MicYou 要求文件名不带 lib 前缀）。

## 安装

1. 验证：`micyou-cli.exe plugin validate <本目录>`
2. 拷贝 `plugin.json` + `box0_voice_link.dll` 到 `%APPDATA%/micyou/plugins/dev.box0.voicelink/`
3. 重启 MicYou，设置 -> 插件 里启用 "Box0 Voice Link"

验证：`netstat -ano -p udp | findstr 9125`，端口被 micyou.exe 占用即正常。插件日志在 MicYou 插件面板「日志」标签（前缀 `[voice-link]`）。

## 配置

MicYou 插件卡片上的表单直接改（或编辑 `%APPDATA%/micyou/plugin-state.json` 里本插件的 config）：

| key | 默认 | 说明 |
|---|---|---|
| startKeys | CTRL+WIN+SHIFT | M 短按开始语音输入（支持 CTRL/ALT/SHIFT/WIN/RALT/RCTRL/A-Z/0-9/F1-F12/ESC/SPACE/ENTER/TAB/BACKSPACE/DELETE，+ 连接） |
| stopKeys | CTRL+WIN+SHIFT | M 短按结束语音输入（再按任意键即退出，重发同一组合键即可） |
| holdKeys | ALT+WIN | M 长按期间保持按住的组合键（按住说话，松手释放） |
| enterKeys | ENTER | 发送识别文本 |
| deleteKeys | BACKSPACE | 向左删除一个字符 |
| port | 9125 | UDP 监听端口 |

改配置后需重启 MicYou（插件 init 时读一次）。

## 注意

- `plugin.json` 必须是无 BOM 的 UTF-8，否则 manifest 解析失败
- 快捷键生效要求微信输入法是当前活动输入法；微信语音输入的热键需保持默认（点按=Ctrl+Win+Shift，按住说话=左Alt+左Win）
- 参考文档：MicYou 仓库 docs/plugins/development-guide.md（本目录的 micyou_plugin_abi.h 即拷自该仓库 tauri-app/crates/micyou-plugin/include/）
