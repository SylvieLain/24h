# 当前任务
//每次写入时候全部清除并 简单描述只写当前状态

`source/app/tasks/task.c` 实现任务0独立停车、任务1 A→B（2次状态变化）和任务2 A→B→C→D→A（5次状态变化）。`car_pos` 在路径点更新；任务2在 B/D 沿弧启用循迹，在 A-B/C-D 开环，回到 A 停车。`state_changed()` 比较 OPEN/CLOSE 状态。修正 `app_irtracking.c` 中 `TrackFlag` 条件反向导致启用循迹时不更新误差的问题。上键启动任务1；左键从其他任务/停止态切到任务2时先将逻辑位置设为 A，任务2运行期间重复按左键不重置位置。

2026-09-24 全量 EIDE rebuild 编译、链接及 HEX 输出成功；最新 DAP flash `verified=true`，复位后目标运行且 `Tick` 递增。链接器仍报告 ELF LOAD segment RWX 警告；post-build 复制 ELF 的脚本因 Windows 下 `cp` 不存在而失败。OpenCode Muse 审查未执行：本机服务未运行，且 `opencode.jsonc` 当前格式无效。路径传感器映射和物理路线尚未验证。

2026-09-24 本轮针对任务2“到一个点就停”：确认 `PID_Init` 后速度/转向增益与目标均为零，进入 `TrackFlag=1` 的定时中断会将两轮 PWM 写为零；按路径首次进入该模式的位置是 B，现场停车点尚未确认。现已在 10 ms 中断给循迹段固定基础 PWM 与线误差 P 修正；任务1及任务2的 A-B/C-D 直线段锁定进入时的 `angle_z` 并用 P 转向修正，角度帧超过 250 ms 不更新则停车。左键作为 key2 启动任务2的现有改动保留；A/B/C/D/A 增加蜂鸣与板载灯 80 ms 提示。EIDE 编译、链接、HEX 输出成功；未烧录本轮固件，转向符号、增益、路线计数与 30 秒要求尚待实车验证。

OpenCode muse-spark-1.3-contributor-free 已完成只读审核；发现并已修复 A 点 CLOSE 启动后停车等待 OPEN 的死锁。修复后重新构建、链接、输出 HEX 成功。DAP flash 本轮未完成：flash 命令超时后 OpenOCD 退出，随后 CMD_INFO 失败，未宣称烧录或物理任务通过。

`task_loop()` 已按 `switch (TASK)` 分发：case 0 停止，case 1 执行任务1，default 复位 TASK 并停止，后续任务可继续增加 case。

多角色审核后修正按键门控：上键回调只提交 `TASK=1` 请求，不再在 10 ms ISR 中直接读取可能滞后的 `trace_state`；`task_loop()` 刷新传感器后再确认 CLOSE，避免按键事件被静默丢弃。全量构建成功，尚未烧录本次按键修复。

