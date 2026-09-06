# Project Memory

## 2026-09-06：正点原子 ESP32 AI BOX0 资料包

资料路径：`D:\Workspace\xiaozhi-esp32\【正点原子】ESP32 AI BOX0资料（A盘）`

### 关键结论

- 用户硬件是正点原子 ESP32 AI BOX0，按键只有左键（`-`）、M 键、右键（`+`）。
- 当前仓库 `main/boards/alientek/atk-dnesp32s3-box0/` 是开源小智固件实现；它没有天气时钟、音乐、木鱼、倒计时、电脑监控、电子相册等完整 ATK 应用菜单。
- 用户设备里看到的“天气时钟”属于资料包 `3，原子固件` 的 ATK 工厂固件功能，不属于当前仓库的小智源码。
- 资料包没有提供 ATK 工厂固件源码，只有二进制。因此“天气时钟界面按什么键退出”不能从当前仓库源码严格确认；按三键 LVGL 导航的设计推测，M 短按最可能是返回/确认，左/右用于切换或调节，但需要实机验证。
- 当前开源小智 Box0 源码中，M 短按是 `ToggleChatState()`，左键音量减、右键音量加；这与 ATK 工厂固件的多应用界面不是同一套交互。

### BOX0 硬件资料

- 尺寸：43 × 36 × 20 mm。
- 屏幕：1.54 英寸，240 × 240。
- 扬声器：35208，1 W。
- 麦克风：4015，-32 dB。
- 电池：500 mAh。
- 接口：USB Type-C。
- 当前小智板级代码引脚：右键 GPIO0，M 键 GPIO4，左键 GPIO3；音频 codec 为 ES8311。

### 资料包内容

- `1，固件下载工具`：Flash Download Tool 3.9.7 和烧录参考 PDF。
- `2，小智固件`：
  - `v1.9.2_atk-dnesp32s3-box0.bin`
  - `v2.0.3_atk-dnesp32s3-box0.bin`
  - 对应小智源码压缩包 `xiaozhi-esp32-1.9.2_2.zip`、`xiaozhi-esp32-2.0.3.zip`
- `3，原子固件`：ATK 工厂固件和 SD 卡资源；这是包含天气时钟等应用菜单的固件。
- `4，电脑监控工具`：ATK XCM 电脑监控工具。
- `5，小聆AI`：小聆 AI v1.2.1 固件、源码压缩包和烧录资料。

### ATK 工厂固件功能

从二进制字符串和 SD 卡说明确认，ATK 固件包含这些应用入口：

- 小智 AI
- 天气时钟 / WeatherClock
- 音乐播放
- 木鱼
- 倒计时
- 电脑监控
- 电子相册
- 系统设置

天气时钟相关实现信息：

- 使用 NTP 校时，二进制中出现 `pool.ntp.org`、`cn.pool.ntp.org`、`ntp1.aliyun.com`，时区 `CST-8`。
- 网络失败提示用户进入“小智AI”应用重新连网。
- 自定义天气动画路径：`0:/USER_DEFINED/weather`。
- 天气动画支持 `weather.gif/jpeg/png/bmp`，尺寸不超过 80 × 80，超出则使用默认 weather。
- 二进制中的设置键包括 `weather_req`、`weather_temp`、`weather_hum`、`weather_icon`、`last_city`。

### SD 卡目录约定

ATK 工厂固件使用以下 SD 卡目录：

- `EXPRESSION`：动态表情，仅 AVI；建议 MJPEG、240 × 240、20–25 fps，每个文件不超过约 1.4 MB。文件名：`idle.avi`、`listening.avi`、`sleepy.avi`、`speaking.avi`。
- `MUSIC`：音乐文件，支持 WAV/MP3。
- `PICTURE`：图片，支持不超过 240 × 240 的 GIF/PNG/JPEG/BMP。
- `USER_DEFINED`：自定义界面。
  - `logo.gif/jpeg/png/bmp`：开机动画，不超过 240 × 240。
  - `weather.gif/jpeg/png/bmp`：天气时钟动画，不超过 80 × 80。

### 固件升级规则

- 如果 BOX0 当前是小智固件，要升级成 ATK 固件，必须使用 `3，原子固件\1，从小智固件升级到ATK固件` 下的固件完整烧录。
- 如果 BOX0 已经是 ATK 固件，后续升级使用 SD 卡：把 `dnesp32s3b0_v*.bin` 放到 SD 卡根目录，插入 BOX0 TF 卡槽后复位升级。

### 重要固件哈希

- ATK 完整烧录固件 `dnesp32s3b0_shengchang_bin_v3.0.bin`：SHA256 `2CA247F22633FD127BDC320C78F4636474630D59F90ED158EE1A144EAE974E34`
- ATK SD 升级固件 `dnesp32s3b0_v2.1.bin`：SHA256 `1EA586523F0C941C78D609A271DCB628060B81F057B36B414316313E8F7169A6`
- 小智 `v1.9.2_atk-dnesp32s3-box0.bin`：SHA256 `E381F397CF7FE32D4F586A7D1464BF0C0A1FF44AF8EAA58CFB9AA4B5E4A48FF0`
- 小智 `v2.0.3_atk-dnesp32s3-box0.bin`：SHA256 `8A93C363CBCFF59E01892D2F9EF773A6B55C18545326361F249DAFC129A2AABF`
- 小聆 `xiaoling_atk-dnesp32s3-box0_v1.2.1.bin`：SHA256 `B839D3EA90DE894A3E63AF1624E0AB3E24950EDEFC6FF517E3C2982E2BBD21AC`

### 后续开发注意

- 后续如果要给小智固件增加“天气时钟”或多应用菜单，应基于当前仓库的 Box0 板级代码和 `Display` 接口重新实现，不要误以为资料包里的 ATK 工厂固件源码已经存在。
- 三键交互需要明确设计：建议左/右负责导航或数值调节，M 负责确认/返回；但当前小智源码已占用 M 长按做配网/睡眠，新增菜单时要避免冲突。

## 编译与刷机（2026-09-06 已验证）
- ESP-IDF: D:\Opts\esp-idf-v6.0.2（仅框架源码，工具链由 install.ps1 装到 %USERPROFILE%\.espressif，仅 esp32s3 目标）
- 注意：本机默认 shell 环境带 MSYSTEM 变量，install.ps1/export 前需 `Remove-Item Env:MSYSTEM` 否则被误判为 MSys/Mingw
- 编译: `. D:\Opts\esp-idf-v6.0.2\export.ps1; python scripts/build.py alientek/atk-dnesp32s3-box0 --name atk-dnesp32s3-box0`，产物 build\merged-binary.bin（从 0x0 烧录）
- 刷机: `idf.py -p COM3 flash`（BOX0 当前在 COM3，USB-Serial/JTAG，460800 波特率 OK）
- 监视: `idf.py -p COM3 monitor`，退出 Ctrl+]
- 已验证：v2.4.2 固件启动正常，WiFi 连上 iKuai-1024-2.4G（IP 192.168.9.104），MQTT 激活成功，唤醒词 wn9_nihaoxiaozhi_tts 加载，音频编解码正常
- 芯片实测：ESP32-S3 rev v0.2，8MB Octal PSRAM (AP)，16MB Boya flash，MAC 30:ed:a0:a9:e3:4c

## 自定义功能：开机菜单（2026-09-06）
- 在 atk_dnesp32s3_box0.cc 中加了 LVGL 横版菜单（叠在 lv_layer_top 上），开机显示
- 菜单项：AI 语音（选中后隐藏菜单进入语音界面）、关于（型号/固件版本/编译时间/芯片/MAC）
- 操作：左右键切换选中项，M 键进入，长按左键返回（AI 页长按左键回菜单；关于页长按左键回菜单）
- 坑：菜单字体必须缓存 shared_ptr<LvglFont> 引用（menu_font_），否则 Assets 刷新主题时旧字体被释放，LVGL 绘制崩溃（InstrFetchProhibited @ lv_draw_label_iterate_characters）
- WiFi 硬编码：wifi_board.cc TryWifiConnect() 无保存 SSID 时自动添加 iKuai-1024/wanqing1002
- 注意：若 NVS 已有保存的 WiFi，优先用 NVS 的，硬编码只是兜底

## Flappy Bird 游戏（2026-09-06）
- 菜单第 3 项 Game，左右键循环切换 3 项，M 进入
- 实现：atk_dnesp32s3_box0.cc，LVGL 覆盖层 + lv_timer 30ms 物理循环
- 重力 0.30/帧，M 键 flap 速度 -4.2，管子宽 34、缝隙 78、速度 2px/帧、间距 130px，esp_random 随机缝隙
- 撞管子/上下边界→Game Over，M 重开，长按左键退回菜单；游戏中左右键禁用
- 菜单 UI 文字全英文（开机早期默认字体无中文字形）

## Dino Run 游戏 + 菜单改版（2026-09-06）
- 菜单改为无标题横式列表：4 个整行条目（AI Voice / Flappy Bird / Dino Run / About），选中行蓝色背景，底部提示 L/R Switch M Enter
- Dino Run：浅色底 Chrome 风格，灰恐龙 20x22 在 x=30，地面线在 h-18，绿仙人掌宽14高18-35随机、速度2.5px/帧、间距130-220随机，跳跃速度-5.4重力0.35，按存活时间计分（8帧=1分）
- M 键跳/重开，长按左键退菜单；游戏中左右键禁用
- 注意 PowerShell 批量插入代码块时注意 ArrayList.Insert 只接受2参数、InsertRange 锚点行前不要有多余空行/括号，改完用大括号配平检查

## 无线麦功能（2026-09-06，MicYou 协议）
- 菜单 5 项：AI Voice / Wireless Mic / Flappy Bird / Dino Run / About（行高 36，间距 40，y 起点 8）
- Windows 软件：MicYou（GPL-3.0 开源），https://github.com/LanRhyme/MicYou/releases/latest → MicYou-Win-2.0.3-installer.exe；需配合 VB-Cable 虚拟声卡（软件内可一键装）才能作为系统麦克风
- 协议（从 v2.0.3 源码确认，与最新 release 一致）：桌面端=TCP 服务器端口 9123（UDP 音频 9124），设备端主动连接
  - 发现：mDNS 服务 `_micyou._tcp.local.`（桌面广播自己），ESP32 用 espressif/mdns 组件 mdns_query_ptr 查询（IDF6.0 的 mdns 是托管组件，已在 main/idf_component.yml 加 espressif/mdns: ^1）
  - 握手：连上后发 "MicYouCheck1"，收 "MicYouCheck2"，首帧发 ConnectMessage{sessionId}（随机 int64）
  - 帧格式：8 字节头 [magic 0x4D696359 "MicY" BE][payload_len i32 BE] + protobuf MessageWrapper
  - 音频（TCP-only 模式）：MessageWrapper.audioPacket=AudioPacketMessageOrdered{seq, audioPacket{buffer, 16000, 1, audioFormat=2(PCM16), codec=0}, timestamp_ms, sessionId}，20ms 一包（320 采样=640B）
  - 服务端每 500ms 发 ping，需排干 RX 并回 pong（否则 ~2 分钟后 TCP 窗口满被踢）；客户端 10s 无帧也会超时，空闲时每 3s 发 ping 保活
- 实现全在 atk_dnesp32s3_box0.cc：ShowMicPage/ExitMicPage/MicTaskRun 等，手写 protobuf varint 编码（无需 protobuf 库）
- 交互：进入页面自动 mDNS 发现+连接（显示本机 IP 和 PC IP），M 键开始/停止推流，长按左键退出
- 推流时 EnableWakeWordDetection(false)+EnableVoiceProcessing(false) 接管 I2S 输入（AudioService::ReadAudioData 公共接口直接读 16kHz PCM），页面打开期间 power_save_timer_ 禁用防休眠，退出时恢复
- BOX0 实测 ES8311 为单声道 16kHz（AFE 日志确认 1 microphone），代码仍兼容双通道取左声道

## 无线麦联动 PC 语音输入（2026-09-06）
- 方案：BOX0 在 M 键切换推流时向 PC（mDNS 发现的 IP，存 mic_pc_ip_）发 UDP 9125 信号（"start"/"stop"）；PC 端跑 mic_hotkey.py（仓库根目录）监听并模拟 Ctrl+Win 拉起/关闭微信语音输入
- 微信电脑版 4.1.7+ 语音输入快捷键 Ctrl+Win（开关式，不用长按；可在微信输入法设置-快捷键里自定义）
- 退出无线麦页面时若在推流也会补发 "stop"
- PC 端脚本首次运行需在 Windows 防火墙放行 python.exe（专用网络）
- 备选方案（未用）：MicYou v1.1.0+ 自带 HTTP API（端口 13333，POST /start /stop）；MicYou 插件系统 PluginMessage 跨端消息；Windows 自带语音输入 Win+H
- 注意：COM3 被残留 idf_monitor/esp_idf_monitor 进程占用会导致刷机 PermissionError，先 Stop-Process 清理

## 2026-09-06 后续调整
- 无线麦联动 PC 语音输入功能已按用户要求整体删除（UDP 9125 信号 + mic_hotkey.py 都已移除），无线麦本体保留
- About 页面扩充：型号+固件版本、编译时间、芯片版本+Flash 大小、MAC、SSID、IP、RSSI、空闲 Heap/PSRAM；注意 spi_flash.h 不在 main 的 include 路径，Flash 大小直接硬编码 16MB
- About 正文改为 LV_ALIGN_TOP_MID y=44、行距 2（内容变多后居中放不下）

## About 分页改版（2026-09-06）
- About 改为 2 页（kAboutPages=2），左右键翻页（SelectAboutPage 循环切换），标题显示 About 1/2，底部提示 L/R: Page Hold LEFT: Back
- 第 1 页（重要信息）：型号+固件版本、编译时间（__DATE__ 运行时解析转 yyyy-mm-dd hh:mm 省空间）、IP、SSID、RSSI、MAC、电量（PowerManager::GetBatteryLevel() 公有，battery_level_ 是私有的）
- 第 2 页（硬件）：芯片版本 esp_chip_info、CPU 核数、RAM/PSRAM 总容量+空闲（heap_caps_get_total/free_size）、Flash 16MB、开机时长
- 坑：GCC -Werror=format-truncation 会把 snprintf %d 按 int 最坏 11 位计算，值用 %100 取模收窄范围即可消除

## 2026-09-06 游戏手感与还原度优化
- 两个游戏帧率 33fps -> 50fps（lv_timer 30ms -> 20ms），全部物理参数按新帧率重调
- Flappy Bird 还原原版：天蓝底 0x4EC0CA、绿色管道加深绿描边和两端帽沿（pipe_cap_top/bottom_）、底部草地+土地条（kFlappyGroundH=26）、小鸟改为黄身体+橙嘴+眼睛并随速度倾斜（transform_angle，枢轴在中心）；flap -5.3、重力 0.34、终端速度 6.5、管道速度 2.6 起随分数微增至 3.2、撞天花板不死只会贴顶、碰撞加 2px 宽容
- Dino Run 还原 Chrome 原版：纯白底+深灰 0x535353 元素、恐龙为容器（身体+头+眼睛+双腿，地面跑动双腿每 6 帧交替）、仙人掌 3 种尺寸（12x24/14x30/16x36，新增 cactus_w_）、云朵视差（0.35x 速度）、地面虚线全速滚动、速度 3.4 起随分数增至 6.0、生成间距随速度缩放（170+speed*22+rand60）保证可跳过、计分改 5 位数 %05d、跳跃 vy=-6.4 g=0.42

## 2026-09-06 游戏 bug 修复 + 本地仿真环境
- 根因修复：PowerSaveTimer(60s 变暗/300s 关机) 在游戏期间不会被 M 键重置，导致 Flappy 按 M 无反应（按键被唤醒分支吞掉+屏幕近黑）、Dino 玩到一半"卡死"（60s 定时器触发降亮度+LVGL 降频）。修复：StartGame/StartDinoGame 里 power_save_timer_->SetEnabled(false)（内部自动 WakeUp 恢复亮度），Stop 时 SetEnabled(true)
- 重构：两个游戏抽成平台无关类 Box0Flappy/Box0Dino，放在 main/boards/alientek/atk-dnesp32s3-box0/box0_games.h，通过 Box0GamePlatform 注入 random/get_font/lock/unlock 钩子（设备端 lock 用 new DisplayLockGuard(display_)）。固件与仿真器共用同一份游戏代码
- 本地仿真器 D:\Workspace\box0-sim：winlibs MinGW g++ 16.2（D:\Opts\winlibs\mingw64）+ SDL2-2.32.10-devel + LVGL 9.5 直接用仓库 managed_components 源码编译。build.bat 一键构建 box0_sim.exe（gcc -c 编 LVGL，g++ 编 main.cpp 链接）。操作：左右方向键切换、Enter=M 键、Esc=长按左键返回。以后调游戏手感先在仿真器里跑，改 box0_games.h 即可

## 2026-09-07 无线麦联动微信语音输入 + 波形显示
- 固件：M 键开始/停止推流时向已发现的 PC IP 的 UDP 9125 发送 VOICE_START/VOICE_STOP（MicSignalVoice，成员 mic_pc_ip_ 在 mDNS 发现后记录）；退出页面时若正在推流自动补发 STOP
- 固件 UI：删除 "M: Start/Stop" 和 "Hold LEFT: Back" 两个底部提示，状态文本 "Connected - M: start" 改为 "Connected"；Streaming 红字保留；新增 20 根滚动波形条（mic_bars_，底部 h-24 基线，7px 宽 11px 间距，绿色 0x4ADE80）：推流任务每帧算峰值存 mic_level_（atomic），MicWaveTick 以 40ms lv_timer 滚动刷新并带 3/4 衰减平滑
- PC 端 D:\Workspace\box0-sim\voice_link.exe（voice_link.cpp，build_voice_link.bat 用 winlibs g++ 编译，-lws2_32）：监听 UDP 9125，VOICE_START -> SendInput 模拟 Ctrl+Win+Shift 单击（微信输入法语音输入快捷键，无需长按），VOICE_STOP -> ESC。配置在同目录 voice_link.ini（start_keys/start_hold_ms/stop_keys/port），首次运行自动生成。已端到端自测（收包+按键日志正常）

## 2026-09-07 无线麦页面右键 = PC 端 Enter
- 固件：MicSignalVoice 泛化为 MicSignalPc(const char* msg)；无线麦页面上右键点击发送 UDP "VOICE_ENTER"（无论是否 streaming 都可用，方便 M 停止后按右键发送文字）
- voice_link.exe：新增 enter_keys 配置（默认 ENTER），处理 VOICE_ENTER 消息 -> SendInput Enter；已用测试配置自测通过
- 注意：voice_link.exe 运行中重新编译会 ld Permission denied，先 Stop-Process -Name voice_link

## 2026-09-07 MicYou 插件版 voice_link（替代独立 exe）
- 源码：D:\Workspace\box0-sim\voice-link-plugin\（voice_link_plugin.cpp + plugin.json + build.bat，用 winlibs g++ -shared -static 编译，产物 box0_voice_link.dll，注意 MicYou 要求 dll 文件名无 lib 前缀）
- 依据官方文档 docs/plugins/development-guide.md（本地克隆 $env:TEMP\micyou）：Native cdylib + C ABI（micyou_plugin_abi.h，只导出 micyou_plugin_info/init/deinit，只保存 host 表 frozen 前缀的 log/get_config/ctx 三个字段防 ABI 越界），插件目录 %APPDATA%\micyou\plugins\dev.box0.voicelink\，启用状态写 plugin-state.json
- 功能与独立版一致：UDP 9125 收 VOICE_START/VOICE_STOP/VOICE_ENTER -> SendInput 模拟 Ctrl+Win+Shift / ESC / Enter；快捷键可在 MicYou 插件卡片配置表单改（configSchema），线程用 200ms 收包超时实现干净退出
- 验证：micyou-cli plugin validate 通过；安装后重启 MicYou，netstat 确认 9125 由 micyou.exe 占用，发 VOICE_STOP 测试包进程正常
- 注意：plugin.json 不能有 UTF-8 BOM（PowerShell Set-Content -Encoding UTF8 会加 BOM，需用 UTF8Encoding($false) 重写）；voice_link.exe 与插件不能同时运行（端口冲突），独立版已弃用
- MicYou 安装在 C:\Program Files (x86)\MicYou（含 micyou-cli.exe 可 validate/package 插件）

## 2026-09-07 WiFi 配置外置 + 一键刷机脚本 + PC 工具入库
- WiFi 硬编码从 main/boards/common/wifi_board.cc 还原回上游；兜底逻辑挪到板级文件 EnsureDefaultWifi()，凭据来自自动生成的 box0_local_config.h
- 新增 box0_config.ini（仓库根，用户可编辑的纯文本配置）+ scripts/box0_gen_config.py（ini -> box0_local_config.h 生成器，内容不变时不重写避免触发重编）
- 新增 flash_box0.bat（仓库根）：export ESP-IDF 环境 -> 生成配置 -> 编译 -> 刷机，一键完成；注意必须 set "MSYSTEM=" 否则 export.bat 拒绝运行
- pc-tools/box0-voice-link：MicYou 插件源码入库（含 README）；pc-tools/box0-sim：游戏模拟器源码入库（build.bat 参数化 MINGW_DIR/SDL2_DIR）
- docs/box0-dev-notes.md：给新机器/新 Agent 的完整上下文文档（功能地图、协议摘要、已知坑）
- 环境注意：系统 python 变成 WindowsApps 失效占位符，脚本里要用 ESP-IDF 环境的 python.exe；exec 传输层会处理反斜杠转义（\n 会变真换行），生成文件时避免手写反斜杠转义序列，用 chr() 或正斜杠
