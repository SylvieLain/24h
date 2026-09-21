# 历史任务与决策

仅在形成决定、完成验收或留下重要失败经验时追加，默认只读最新一条。格式：时间 | 角色 | 任务 | 理由与决定 | 验证与遗留。

- 2026-09-05 15:41 +0800 | Brain | TI-AGENT-RULES-20260905 | 用户授权建立 TI 下位机约束及后续模板。保持双角色可替换、适度质疑、最小实验、禁止无依据补偿与保护；保留全部旧工程内容。当前旧云台协议和控制不能当作滚球链路完成证据。 | 本次仅文档；构建、烧录、上下位机联动与物理控制均未验证。
- 2026-09-18 | Brain | TI-HCAR-20260918 | H 题约束归集：当前 Brain 模型与子代理模型均无法读取 PDF，改用隔离临时 venv 的 pypdf 成功提取原文（未改 tools/OpenOCD/.venv）。只读盘点发现 9.5 源码当前不可编译、现有 ELF/HEX 为 revenge\IRP 旧产物、OpenOCD 面向 STM32F103；按项目规范未并发写 STATE。 | 题目几何图待官方复核；构建、烧录、DAP 连接与物理循迹当时均未验证。
- 2026-09-18 | Brain | TI-HCAR-20260918-DAP | H 题配套链路：编译基线 + 调试工具重定向。决定：移除 Jetson 视觉球模块（excludeList + board.c，文件保留）、新建 app_ui 承接共享状态、按键回调用临时代管语义、RunFlag=0 显式清零 PWM（用户授权的安全边界）；OpenOCD 缺 mspm0 驱动改经 gh-proxy 安装 xpack 0.12.0-7（校验官方 SHA-256，落地 tools/openocd_xpack_0.12.0-7）；dap CLI 增 float 写入（_encode + 类型感知，位域拒绝）。接受 Executor 越权的 stepper_motor Light_ON→BL_ON 编译修复，并在注释更正为“实为蜂鸣器接口”。 | 配置解析 CFG_PARSE_OK/1000kHz/exit 0、CLI 离线往返 0 失败为已验证；烧录、探针 runtime、物理循迹均未验证。
- 2026-09-18 | Brain | TI-HCAR-20260918-FLASH | 烧录曾 Verify Failed 且烧后假 running：根因是 mspm0l.cfg 的 reset 处理在 program/reset run 后把 CPU 留在 NMI（PC=0x440 Default_Handler，IPSR=2），Tick 不增。改用 xpack 版本匹配的 `[find target/ti_mspm0.cfg]` 后问题消失；确认探针 Horco CMSIS-DAP v2 serial 183B2401。决定：openocd.cfg 固定用 ti_mspm0.cfg，不再用根目录 mspm0l.cfg（后者保留给 eide.yml）。 | 已验证：program verify OK、dap flash ok:true verified:true、doctor liveness 递增、float 读写回环；电机/循迹等物理行为仍未验证。
- 2026-09-21 | Brain | TI-HCAR-20260921-PINS | 依据原理图、Gerber 飞针网表、SysConfig 与 J-Link 实测，蜂鸣器映射为 PA11/U16_14/BEEP；修复左键关闭测试时误用 `BL_Toggle()` 为 `BL_OFF()`。A/B 仅两路电机通道，不能用软件独立修正同通道两只轮子的相反极性。E1 手转确认对应左后，E2 当前无反馈，先测 CN3-6/7 物理信号再决定改线。 | 新 ELF 编译、J-Link 烧录、两段有效 Flash 回读、Tick 运行通过；用户未听到蜂鸣器，右侧编码器物理链路和最终电机接线未验收。
- 2026-09-21-16:32 |user| 总结 |初步完善硬件问题 着手写定距定角度并完成题目 
- 2026-09-21 | user/Brain | TI-HCAR-20260921-TASK1-RELOCATE | 按用户要求保留 `TASK`、`open_loop1()`、`open_loop2()`，将任务逻辑放入 `source/app/tasks/task.c`；全局 enum 为 `OPEN/CLOSE`，while 实时识别，OPEN→CLOSE 时停车蜂鸣。 | EIDE 编译、链接和 HEX 输出成功；仅 post-build 的 Windows `cp` 脚本失败；尚未烧录和 DAP 运行验证。
- 2026-09-21 | Brain | TI-HCAR-20260921-TASK1-AUDIT | 调用 OpenCode `opencode/muse-spark-1.3-contributor-free` 做只读审核。审核发现 A 点 CLOSE 时先停车等待 OPEN 会造成任务死锁；改为按键后立即 `open_loop1()` 驶出 A 点，再检测 OPEN→CLOSE 停车蜂鸣。 | 修复后 EIDE 编译、链接、HEX 输出成功；DAP flash 超时并触发 CMD_INFO 失败，未完成烧录和实机流程验证。
- 2026-09-21 | user/Brain | TI-HCAR-20260921-TASK-SWITCH | 按用户要求将 `task_loop()` 改为 `switch (TASK)` 任务分发：case 0 停止，case 1 执行任务1，default 复位并停止，保留原 `TASK/open_loop1/open_loop2` 接口。 | EIDE 编译、链接和 HEX 输出成功；post-build 的 Windows `cp` 失败仍存在。
- 2026-09-21 | Brain | TI-HCAR-20260921-KEY-LAG | OpenCode 多角色只读审核确认偶发按键失效的主要根因是按键 ISR 直接用可能滞后的 `trace_state==CLOSE` 门控，导致事件静默丢弃；4 kHz TIMG8 和 10 ms TIMER0 未见源码级主循环饥饿证据。最小修复为按键只置 `TASK=1` 请求，由 task_loop 刷新传感器后确认 CLOSE。 | 全量 EIDE 编译、链接、HEX 输出成功；本次按键修复尚未烧录和实机验证。
