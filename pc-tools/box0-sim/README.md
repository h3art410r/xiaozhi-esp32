# BOX0 PC 游戏模拟器

在 Windows 上用 LVGL SDL2 后端运行 BOX0 的两个游戏（Flappy Bird / Dino Run）。**游戏代码与固件共享同一个文件**：`main/boards/alientek/atk-dnesp32s3-box0/box0_games.h`——调手感只改这个头文件，模拟器验证满意后再刷固件。

## 操作

| 键盘 | 对应 BOX0 |
|---|---|
| ← / → | 左右键（菜单切换） |
| Enter | M 键（进入 / 跳） |
| Esc | 长按左键（返回菜单） |

## 依赖

1. **MinGW-w64 g++**（winlibs，环境变量 `MINGW_DIR`，默认 `D:/Opts/winlibs/mingw64`）
2. **SDL2 mingw 开发包**（https://github.com/libsdl-org/SDL/releases 下载 `SDL2-devel-*-mingw.zip` 解压；环境变量 `SDL2_DIR` 指向含 `x86_64-w64-mingw32/` 的目录，默认 `D:/Workspace/box0-sim/SDL2-2.32.10`）
3. **LVGL 源码**：直接用本仓库 `managed_components/lvgl__lvgl`。该目录被 gitignore，**需先在本机成功编译过一次固件**才会出现（idf 会自动从组件仓库下载，版本锁定在 main/idf_component.yml：~9.5.0）
4. 可用的 `python` 命令（用于生成源文件列表；若系统 python 是 WindowsApps 占位符，用 ESP-IDF 环境的 python 或装一个真 Python）

## 构建与运行

```bat
build.bat        :: 生成 box0_sim.exe（LVGL 对象文件缓存在 obj/，只改游戏代码时重编很快）
box0_sim.exe     :: 240x240 画面、3 倍放大窗口
```

调游戏手感的位置：`box0_games.h` 里的常量（重力/冲量/速度/缝隙等）和 Update() 逻辑。
