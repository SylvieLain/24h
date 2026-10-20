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
- 2026-09-24 | Brain | TI-HCAR-20260924-TASK12-STEER | 任务2按代码到 B 后 `TrackFlag=1`，原 10 ms ISR 使用初始化后全零的速度/转向 PID，导致 PWM 归零；现场停车点未核实。按题图保留 A 出线、B 入线、C 出线、D 入线、A 出线的 5 次变化顺序；直线段用 JY901S `angle_z` 锁定航向并以固定基础 PWM 加 P 差速，半弧段用线误差 P 差速。已有左键任务2请求保留；A/B/C/D/A 加蜂鸣和板载 LED 提示。 | EIDE 编译、链接、HEX 输出成功；本轮未烧录，电机转向符号、增益、点位与 30 秒实车结果待测；post-build `cp` 仍失败。
- 2026-09-28 | Brain | TASK-CLEANUP | 用户授权整理混乱的任务逻辑与注释。合并重复启动标志，明确car_pos为已确认点，任务1/2分别处理，C段目标互斥设置，补偿只作用C-D；保留用户PWM3000、循迹P1200/限幅9999、C补偿-10及航向参数。修复提前位置赋值/目标覆盖与PD初始化D跳变。 | 实际C源主机流程测试、EIDE编译链接/HEX、diff检查通过；未烧录，实车效果待测，cp/RWX旧问题保留。
- 2026-09-28 | Brain | TASK3-DIAGONAL | 用户确认首圈A手动对准对角线、右键Key3启动、B出弧补偿先-14°。实现A-C-B-D-A；首段固定Z角，另一对角目标由首段+100°-180°-14°推算，open_loop2复用定航向控制；共享循迹输出允许TASK3。保留任务1/2当前参数，未实现任务4圈数。 | 实际C源及按键主机测试、EIDE编译链接/HEX、diff检查通过；烧录初始化失败/CMD_INFO failed，verified=false，待探针恢复，实车未验收。
- 2026-09-28 | Brain | TASK3-EXIT-RELATIVE | 用户澄清转向方向正确，但实际转约130°而非出弯后50°。B→D目标改成出弯实测angle_z+50°+独立补偿(-14°)，净目标角差+36°；保留手动首段、Key3、任务1/2参数。 | 实际C源相对角/±180°/回归测试与EIDE构建通过；探针按用户要求暂不处理，未烧录本修正版，实车待验。
- 2026-09-30 | Brain/Executor | TASK4-THREE-LAPS-OPEN-A | 用户明确任务1/2/3/4均从A点OPEN启动，任务4独立跑三圈并在前两次返回A后自动重新定向；任务3首段0°保持现状，任务4后续A目标用启动时Z角。 | 实际task.c主机三圈与任务1/2/3路线回归、EIDE编译链接HEX、DAP verified=true/target_running=true通过；实车三圈与±5cm定距未验证。
- 2026-09-30 | Brain/Executor | LINE-HEADING-OSCILLATION | 实车报告剧烈摆动后源码确认：循迹差量超过base3500却跳赋±PD_LIMIT9999导致单侧反转；航向Kp由既有30被调成1200，而begin_heading_hold不重设Kp。最小修复为循迹限幅±base、航向Kp恢复30，独立LINE_KP1200保持。 | EIDE编译链接HEX、输出范围计算、DAP verified=true/target_running=true；实车震荡改善待验证。
# 2026-10-18 任务3/4斜入弯定向捕线

用户批准参考2024电赛H题同题方案，将AC左转入弯、BD右转入弯独立为捕线阶段，替换触线后直接交给质心循迹的旧入口逻辑。用实时探头条件结束捕线，不加固定出弯延时；捕线期间全白不推进位置，完成后恢复原加权PD与即时出口。仅入口动作用内轮0/外轮7000，不照搬他人内轮反转/阻塞while。任务1/2、现有增益及任务4原圈数保留。当前完成条件为对侧中央探头见黑、外侧均白且中央连续1~3路黑，需实车验收。软件组合/流程检查与EIDE编译通过，未烧录。证据见STATE.md和work/task34_capture_20261018。

# 2026-10-19 按键定义、试跑漏录和调试项目混用纠正

用户要求记录本次错误：先前现场按中键期望任务3，固件却执行任务4；现统一中键任务3（一圈）、右键任务4（三圈），以当前用户定义为准。旧采集器只识别任务3并在reset暂态退出，导致后续试跑漏录；不能把只录到的一段TASK4当三次TASK3证据。记录器改为保存实际TASK、按键边沿和每次独立结果，reset/断链结束当次但继续等待读取恢复。不得把同端口另一个STM32工程服务按TI符号解码；采集前核对targets、板上HEX、对应ELF和Tick。

本次烧录OpenOCD出现CRC算法超时，虽然最终Verified OK，仍独立逐字节回读17260字节与HEX一致后才放行。停车状态实际reset恢复通路测试197点通过，纯主机序列亦验证reset/断链后可接受后续按键。试跑期间reset按紧急失败保留，主机不自动reset、resume或启动任务。完整错误与证据记录见work/task3_dap_20261019/FINDINGS.md、flash_prepare_20261019/。


2026-10-19 用户明确统一DAP至D:\CODE\mcu_tool并上传新私有库vifArgent_dap；原项目仅保留兼容入口与旧文件，避免干扰现场。切项目只改本机配置，不自动更换占用探针的OpenOCD服务；真实访问前核对精确目标名称。TASK34宏不能直接热调，用户选择先完成工具、只生成固件补丁。主机输出分解和非原子采样保留证据限制，不冒充同周期实际P/D。

2026-10-19 用户要求DAP是通用工具，不能过拟合当前路线。落实为：工程符号/状态规则/阶段展示/默认值/模拟波形均在profiles；核心仅有采集、明确公式、配置标签和通用界面。TI A→C→B→D→A仅为可选适配，温控示例用同一核心验证。项目工具目录只留使用（人类与AI）和维护MD，不再保留可执行副本。配置回写曾使EIDE恢复JLink Git基线，证据不足归因；Luna重设Custom后重复静态复读通过，未擅自操控IDE/探针。

2026-10-20 用户用复位/断电作停车操作时，连接丢失与人为动作吻合，应保留该段有效记录、在重新上电后由用户明确手动重连，不按unknown自动恢复或复位。连接失败需区分HTTP服务、OpenOCD端口、USB、CPU target状态；不要让笼统not running误导用户。
