# Box0 Voice Link（MicYou 原生插件）

把 BOX0 无线麦变成一个"持续录音 + 桌面快捷键"设备。只要 MicYou 在运行，联动就在——不需要独立进程。

## 工作链路

```
BOX0（无线麦页面）
  自动发现 PC -> 自动连接 -> 自动开始持续推流
  全程录音（PC 端按会话分轨保存，退出页面自动停止并混合）
  左/右键循环切换桌面快捷键（屏幕显示 KEY: VOICE/ENTER/DEL）
  M 短按发送当前选中的快捷键
  M 长按（约0.8s）暂停/恢复录音
  左键长按返回菜单
    -> UDP 9125 发送 REC_START / REC_STOP / KEY_SEND n
MicYou 插件（本目录编译出的 box0_voice_link.dll）
    -> 录音：实时音频回调抓麦克轨 + WASAPI loopback 抓系统声音轨
    -> 停止时写出 rec_<时间>_{mic,sys,mix}.wav 三个文件
    -> SendInput 模拟桌面快捷键
```

固件侧发信号代码：`main/boards/alientek/atk-dnesp32s3-box0/atk_dnesp32s3_box0.cc` 的 `MicSignalPc()`（目标 IP 来自 mDNS 发现 MicYou 的结果，固件端常量 `kMicVoicePort = 9125`）。

## 录音文件

每次进入无线麦页面 = 一个录音会话（连接成功自动开始，退出页面/断线自动结束）：

- `rec_YYYYMMDD_HHMMSS_mic.wav` —— BOX0 麦克风（来自 MicYou 实时音频流，16-bit PCM）
- `rec_YYYYMMDD_HHMMSS_sys.wav` —— PC 系统声音（默认输出设备 loopback，WAV 格式同上）
- `rec_YYYYMMDD_HHMMSS_mix.wav` —— 两轨自动混合（采样率对齐后叠加，增益见配置）

默认存放 `%USERPROFILE%\Documents\box0-rec`。麦克帧中断超过 3 秒（如 MicYou 退出）自动结束会话。录音期间快捷键全部可用。

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
| startKeys | CTRL+WIN+SHIFT | 快捷键 0（VOICE，M 切到它后右键发送） |
| enterKeys | ENTER | 快捷键 1（ENTER） |
| deleteKeys | BACKSPACE | 快捷键 2（DEL；左键短按也发送它） |
| holdKeys | ALT+WIN | M 长按期间保持按住的组合键（按住说话） |
| recDir | （空） | 录音存放目录，空 = `%USERPROFILE%\Documents\box0-rec` |
| micGain | 1.0 | 混合时麦克轨增益 |
| sysGain | 1.0 | 混合时系统声音轨增益 |
| port | 9125 | UDP 监听端口 |

改配置后需重启 MicYou（插件 init 时读一次）。

## 注意

- `plugin.json` 必须是无 BOM 的 UTF-8，否则 manifest 解析失败
- 快捷键生效要求微信输入法是当前活动输入法；微信语音输入的热键需保持默认（点按=Ctrl+Win+Shift，按住说话=左Alt+左Win）
- 系统声音轨依赖默认输出设备；录音中途切换默认设备不会中断（保持在开始时的设备）
- 参考文档：MicYou 仓库 docs/plugins/development-guide.md（本目录的 micyou_plugin_abi.h 即拷自该仓库 tauri-app/crates/micyou-plugin/include/）
