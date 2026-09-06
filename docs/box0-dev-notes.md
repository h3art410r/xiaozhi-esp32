# BOX0 定制开发笔记（给开发者和 Agent 的上下文文档）

本仓库是 xiaozhi-esp32 的 fork，目标硬件只有一款：**正点原子 ATK BOX0**（ESP32-S3，240x240 ST7789 方屏，ES8311 声卡，3 个按键：左 GPIO3 / M GPIO4 / 右 GPIO0，串口默认 COM3）。上游的板级适配已存在；我们的定制全部集中在少数几个文件里，改代码前先看这份文档和根目录的 `memory.md`（变更日志）。

## 日常操作

| 操作 | 方法 |
|---|---|
| 改 WiFi 等本地配置 | 编辑根目录 `box0_config.ini`（不要改代码） |
| 编译+刷机（一键） | 双击根目录 `flash_box0.bat`（先改 ini 再双击）。环境变量 `ESP_IDF_DIR`（默认 D:/Opts/esp-idf-v6.0.2）、`BOX0_PORT`（默认 COM3） |
| 调游戏手感 | 改 `main/boards/alientek/atk-dnesp32s3-box0/box0_games.h`，先在 PC 模拟器验证：`pc-tools/box0-sim/build.bat` 后运行 box0_sim.exe |
| 手动编译 | `python scripts/build.py alientek/atk-dnesp32s3-box0 --name atk-dnesp32s3-box0`（需先 source ESP-IDF 环境） |

**新电脑首次构建需要联网一次**下载 managed_components（LVGL 等）；之后全流程离线可用。`flash_box0.bat` 内部会先用 `scripts/box0_gen_config.py` 把 `box0_config.ini` 生成为 `box0_local_config.h`（自动生成，勿手改），再编译刷机。

## 定制功能地图

| 功能 | 位置 |
|---|---|
| 开机菜单（5 项横式列表，左/右切换，M 进入，长按左返回） | `main/boards/alientek/atk-dnesp32s3-box0/atk_dnesp32s3_box0.cc` |
| Flappy Bird / Dino Run 游戏 | 同目录 `box0_games.h`（平台无关，与 PC 模拟器共享） |
| 无线麦（MicYou 协议，推 PCM 给 PC 当麦克风） | 板级 .cc 的 Mic* 系列函数 |
| 无线麦 -> 微信语音输入联动 | `pc-tools/box0-voice-link`（MicYou 插件，UDP 9125 收 VOICE_START/STOP/ENTER -> SendInput 快捷键） |
| About 页（2 页设备信息） | 板级 .cc 的 About* 函数 |
| WiFi 免配网兜底 | 板级 .cc `EnsureDefaultWifi()`（读 box0_local_config.h）。**不要改 main/boards/common/wifi_board.cc**，那是共享代码，必须保持上游原样 |

## 无线麦协议摘要（MicYou v2）

1. mDNS 发现 `_micyou._tcp`（MicYou PC 端默认 TCP 9123）
2. 握手：发 12 字节 `MicYouCheck1`，收 `MicYouCheck2`，再发 protobuf ConnectMessage（含 session id）
3. 推流：8 字节帧头（magic `0x4D696359` + 大端长度）+ protobuf 包装的 PCM（16bit 单声道 16kHz，20ms/帧 640B）
4. M 键开始/停止推流；推流时禁用唤醒词和语音处理来接管 I2S；页面打开期间禁用省电定时器
5. M/停止/右键同时向 PC UDP 9125 发控制信号（voice-link 插件）-> 微信语音输入快捷键

## 已知坑（血泪史）

- **MSYSTEM 环境变量**：存在时 ESP-IDF 的 export.bat 直接拒绝运行，脚本里必须 `set "MSYSTEM="`（flash_box0.bat 已处理）
- **python 别名**：Windows 的 `python` 可能是 WindowsApps 占位符（无法运行）。可靠的 python：`%USERPROFILE%/.espressif/python_env/idf6.0_py3.14_env/Scripts/python.exe`
- **plugin.json 不能有 UTF-8 BOM**（PowerShell 5.1 的 Set-Content -Encoding UTF8 会加 BOM）
- MicYou 插件 dll 文件名**不能带 lib 前缀**
- GCC `-Werror=format-truncation`：snprintf 的 %d 按 11 位算，必要时用 %100 收窄
- 菜单字体必须在创建 UI 时从 `LvglTheme->text_font()` 重新取（主题刷新后旧指针失效会崩）
- 省电定时器 `PowerSaveTimer(-1, 60, 300)` 在游戏/无线麦页面必须 `SetEnabled(false)`，否则 60 秒后屏幕变暗且按键被"唤醒分支"吞掉
- COM3 被残留的 idf_monitor 进程占用会导致刷机失败，先杀掉 python monitor 进程

## PC 配套工具

- **MicYou**（开源，https://github.com/LanRhyme/MicYou）：PC 端麦克风接收 + 虚拟声卡输出。插件开发文档见其仓库 docs/plugins/
- `pc-tools/box0-voice-link/`：MicYou 原生插件（本仓库维护，见其 README）
- `pc-tools/box0-sim/`：游戏 PC 模拟器（见其 README）

## 跨网络说明

整套链路（mDNS 发现、TCP 推流、UDP 控制信号）只在**同一局域网**内工作。设备带到别的网络需先在 box0_config.ini 里改 WiFi；要跨公网用需自行组 VPN（Tailscale 等）并让固件直连 PC 的虚拟 IP（当前未实现）。
