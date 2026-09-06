# BOX0 软件 Idea List

基于 BOX0 的小屏幕、3 个物理按键、麦克风、扬声器、Wi-Fi 等能力，可以尝试把它做成各种「小型专用设备」，不必追求完整的 GUI 或手机式交互。

## 🎮 小游戏机

* **贪吃蛇**：Left / Right + M，最容易做成完整体验。
* **俄罗斯方块**：利用三个键设计极简操作。
* **2048**：适合小屏幕，操作可以做得非常简单。
* **赛车游戏**：左右控制换道，M 做加速/特殊技能。
* **Pong / 打砖块 / Flappy Bird**：适合 ESP32 的轻量小游戏。
* **小游戏合集**：开机后通过 Left / Right 选择游戏。

## 🐣 AI 电子宠物

* 做一个类似 Tamagotchi 的小宠物。
* 有心情、饥饿、睡眠、成长等状态。
* 可以通过麦克风和它说话，AI 负责生成性格和对话。
* 小屏幕显示宠物表情，扬声器负责叫声和回复。
* 可以让宠物根据每天的互动形成长期记忆。

## 🤖 AI / Agent 遥控器

* 把 BOX0 做成一个**实体 Agent Button**。
* 长按 M 说话，把语音发送给 PC 上的 Agent / Codex。
* Agent 返回结果后在屏幕上显示，并通过扬声器播报。
* 可以用 Left / Right 浏览 Agent、任务、状态。
* 例如直接问：「刚才 CI 为什么挂了？」、「帮我重新跑一下」。
* 这是一个比较值得重点探索的方向：**让 Agent 从电脑里的软件变成桌面上的实体设备。**

## 🖥️ 桌面信息 HUD

把 BOX0 放在显示器旁边，作为一个常驻的小型状态屏。

可以显示：

* Codex / Agent 当前任务和运行状态
* Git / CI 状态
* CPU / 内存 / GPU / 网络状态
* 当前下载、编译、测试进度
* 天气
* 时间 / 日历
* 股票 / 加密货币行情
* 自己定义的各种 API 数据

Left / Right 切换不同的信息卡片。

## 🌐 网络工具盒

做一个随身的极客网络工具：

* Ping
* DNS 查询
* HTTP 状态检测
* Wi-Fi 信号强度
* 网关 / IP 信息
* Internet 连通性
* 简单端口 / 服务状态检查

适合做成非常简洁的「Pocket Network Monitor」。

## 🎵 网络音乐 / 收音机

* 网络电台
* Podcast 播放器
* Lo-Fi 音乐播放器
* 简单的音乐控制器
* 显示当前歌曲 / 节目名称
* Left / Right 切换频道，M 播放/暂停

## ⏱️ 极简桌面工具

可以做成一个「三个按键的工具箱」：

* Pomodoro
* Stopwatch
* Countdown
* Alarm
* Random Number
* Dice
* Coin Toss
* Todo / Reminder

这些东西不需要复杂 UI，反而非常适合 BOX0。

## 🎲 桌游 / 聚会玩具

利用随机数和小屏幕做一些简单的 Party Game：

* 骰子
* 抽签
* 真心话 / 大冒险
* 随机选择
* 猜数字
* 简单卡牌游戏
* RPG 随机事件 / 掷骰子系统

## 📊 Personal Dashboard

做一个完全属于自己的小型 Dashboard。

例如：

> **Today**

* Focus：3h 42m
* Git commits：8
* Agent tasks：12
* CI：7/8
* Weather：18°C
* BTC：xxx
* TODO：3

每天开机就是一个「个人状态面板」。

## 🕹️ 一个更大胆的方向：BOX0 OS

如果后面做出了很多小应用，可以把它们组合成一个非常轻量的：

**BOX0 OS / BOX0 Launcher**

```text
Games
AI
Agent
Network
Market
Tools
Pet
Music
```

通过 Left / Right 浏览，M 进入。

重点不是做一个复杂操作系统，而是做成一个**极简的实体小工具平台**。

---

## ⭐ 值得优先尝试的几个方向

如果只是为了「玩一玩这个硬件」，我会优先考虑：

1. **AI / Agent 遥控器** —— 最符合 BOX0 的麦克风 + Speaker + Wi-Fi 能力，也最容易和 PC 上的 Agent 结合。
2. **AI 电子宠物** —— 最有趣，容易做出很强的「实体感」。
3. **桌面 Agent / System HUD** —— 实用性最高，可以一直放在显示器旁边。
4. **小游戏合集** —— 最容易快速获得成就感。
5. **Network Tool / Geek Tool** —— 很适合 ESP32，而且可以逐渐扩展成自己的工具箱。

核心思路：**不要把 BOX0 当成一个小型手机，而是把它当成一个有屏幕、有耳朵、有嘴巴、只有三个按钮的实体小 Agent / Tool。**
