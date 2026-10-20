# TI工程的DAP独立工具入口

EIDE原构建/烧录链保持不动。

# 给人类：DAP 快速上手与航向 PD 调试

工具源码统一在 `D:\CODE\mcu_tool`，私有仓库为 [vifArgent_dap](https://github.com/SylvieLain/vifArgent_dap)。工程 `tools/OpenOCD` 仅保留使用和维护 Markdown，不再放脚本、配置、虚拟环境或采集数据。

## 1. 选对工程

```powershell
cd D:\MCU_Code\TI\revenge\9.5
dap project show
dap project use ti-revenge
```

当前工程目录会自动选择 TI。也可始终写 `dap --project ti-revenge ...`。选择项目不连接探针，不会把现有 STM32 服务变成 TI 服务。全局入口失效时，用 `D:\CODE\mcu_tool\bin\dap.cmd` 替代 `dap`。

## 2. 启动服务与烧录

已在运行 TI OpenOCD 时不要重复启动。确实没有服务且探针可用时，在单独终端运行：

```powershell
dap --project ti-revenge server
```

先 F8 构建工程，再执行以下显式烧录命令；烧录会重启程序，并恢复读侧服务：

```powershell
dap --project ti-revenge flash --json
dap --project ti-revenge doctor --json
```

`flash` 使用中央项目配置内的 ELF；请先核对 `project show` 的路径。DAP命令是独立入口，EIDE F9 / Build & Flash 保留用户原来的烧录配置，不由本工具接管。Cortex-Debug 使用 external GDB `localhost:3333` 的 attach，加载符号，不自动下载、重启或运行到 main；F5 本身仍是显式调试操作，调试器可能暂停程序，实车运行中不要开启断点调试。

## 3. 监视指定变量

```powershell
# 一次读取
dap --project ti-revenge read turn_PID.Kp turn_PID.Kd turn_PID.Target turn_PID.Actual --json

# 指定变量实时曲线，10秒后结束
dap --project ti-revenge monitor turn_PID.Target turn_PID.Actual turn_PID.Out --duration 10

# AI/脚本：定时 JSONL，不打开图形
dap --project ti-revenge monitor TASK car_pos TrackFlag --json --duration 5

# 保存原始 CSV
dap --project ti-revenge record Tick TASK car_pos TrackFlag turn_PID.Target turn_PID.Actual turn_PID.Out --rate 10 --duration 30 --out D:\CODE\mcu_tool\runs\heading.csv --json
```

## 4. 打开可视化监控

```powershell
# 在TI工程目录直接使用
dap --visualize

# 显式指定工程
dap --project ti-revenge --visualize

# 完全模拟，不读取或写入MCU
dap --project ti-revenge --visualize --demo
```

页面打开时不访问探针。点“开始采集”才读 MCU；采集和网页刷新相互独立，原始 CSV / 事件逐条保存到 `D:\CODE\mcu_tool\runs\ti-revenge\会话目录`，模拟数据在 `runs\demo`。关闭页面不会让后台采集静默停止；离开前点“停止采集”，或 Ctrl+C 结束面板服务，均不改变 MCU 运行状态。

左侧预置航向 `Kp / Kd / Ki / Target` 滑块，默认分组 **1**。源码参考值为 `Kp=200, Kd=50`；这不证明板上正使用这两个数。采集后显示实际 RAM 值，尚未暂存修改的滑块会跟随实测值。

拖动滑块或填写数值只暂存，点对应“写入”才执行 RAM 写入和读回。一次尽量只改一个关键参数。停止采集后可用“添加条目”登记新参数、目标值、只读反馈或状态位；“编辑”可改显示名、类型、分组、上下限及步长。未指定分组时使用1，也可以输入新的分组ID和名称。保存界面配置不写 MCU。

## 5. 航向 PD 的具体案例

当前源码有：

```c
turn_PID.Kp = 200.0f;
turn_PID.Kd = 50.0f;
```

这两个变量已登记可写，不需要将宏改 RAM。终端也可显式写入：

```powershell
dap --project ti-revenge write turn_PID.Kp 200 --json
dap --project ti-revenge write turn_PID.Kd 50 --json
```

RAM 调参通常在 reset 后恢复固件初值。界面不会改 C 文件。航向 `Target` 可热改，但任务启动和段落切换会按控制逻辑重新设置目标，所以临时目标不保证跨段保持。

## 6. 对照 A→C→B→D→A

选择 `turn_PID.Out` 输出环，开始采集后由你按任务3的实体启动键。面板只观察，不替你启动电机。参考状态：

| 段落 | TASK | car_pos | TrackFlag | 应观察 |
|---|---:|---:|---:|---|
| A→C 航向 | 3 | 0 / A | 0 | 航向目标、实际、误差、输出、左右PWM |
| C→B 循迹 | 3 | 2 / C | 1 | 见线状态、捕线方向、左右PWM；航向输出可能是保留值 |
| B→D 航向 | 3 | 1 / B | 0 | B出口新目标、实际收敛、输出分项与限幅 |
| D→A 循迹 | 3 | 3 / D | 1 | 捕线方向、循迹状态、轮PWM |
| 回A后停止 | 0 | 0 / A | 0 | 软件停止；仍需确认真实终点与停车 |

任务4是多圈，不可将首圈回A仍继续行驶解释为任务3停车失败。

状态时间轴与目标/实际、输出分项、参数历史使用同一主机时间。拖动“查看第N点”，或点击任意曲线，可以同时检查该时刻全部状态、目标、实际和柱状图；点事件可跳转到相应记录。`Tick` 回退单独分段，不把 reset 前后连成一次正常完成。

“导出会话”保存可回放 JSON，“导入会话”进入离线回放并禁用写入；CSV适合其他分析工具。浏览器保留最近20000点，完整记录以中央 `runs` 中逐条落盘的文件为准。网页时间窗可选10/30/60秒或全部。

## 7. 输出分项的含义

旧固件只保存总输出，`Actual1` 在 PID 末尾被覆盖。默认图里的 P/I 是主机重建，残差可能含 D、限幅和异步读取，**不能直接叫精确 D 项**。

`patches/heading-pd-terms.patch` 只增加 `PTerm/ITerm/DTerm` 数值保存，不改变参数初值。需要精确分项时，先审阅补丁、应用、构建、烧录并更新 ELF，再选 `turn_PID.Out (recorded terms)`。本次只生成并检查补丁，未应用或烧录。字段仍非原子读取，不保证同一控制周期一致快照。

`TASK34_LINE_KP/KD` 仍是宏；它们与本次航向 PD 不是同一组参数，热调需另用随附 TASK34 补丁。不要把它们映射成 `turn_PID.Kp/Kd`。

默认较多变量采集为10Hz，短于100ms的瞬态/状态边沿可能漏采；不能凭主机图证明10ms控制周期内的峰值、抖动或完整状态序列。断链、目标halt、ELF中途变化时停止采集并保留数据，工具不会自动 reset/resume 或重连启动控制。


---

# 给AI：工具操作约定

## 授权与入口

统一调用中央 `dap` / `D:\CODE\mcu_tool\bin\dap.cmd`。不要在工程内再创建一套 `.venv`、脚本或配置。先 `project show` 核对当前工程、MCU、目标、ELF和端口。

离线开发用 `symbols`、`param info`、单元测试和 `--visualize --demo`。`doctor/read/monitor/record` 会访问设备，需符合当次授权；`server/flash/write/estop` 是显式硬件操作。不得为了验证主机工具自动操作用户当前调试服务。

选项目不切换服务。目标名称检查只能防明显跨工程访问，不能证明板上程序与ELF一致。变量地址只能来自当前匹配ELF，不能沿用旧录制里的硬编码地址。

## 工程差异在配置中

- `projects.*.json` 选择工程；local配置保存ELF、端口、OpenOCD、RAM、别名、白名单与限值。
- `dashboard` 定义条目角色、分组、设备时间符号及可选阶段规则。
- `loops` 定义输出分项公式、目标与实际符号。
- `demo` 定义模拟信号；没有特殊小车/温控逻辑。

不能把某个工程的任务号、路线、PID符号或默认参数固化进核心。新增工程优先增加配置和相应例子。没有阶段定义就只看原始状态，不替用户猜业务流程。

## 最小操作方式

示意变量必须替换为真实工程符号：

```powershell
dap --project my-board project show
dap --project my-board symbols control --json
dap --project my-board param info control.kp
dap --project my-board monitor control.target control.actual --json --duration 5
```

新增写参先离线核对RAM标量、DWARF类型、白名单和范围。登记/编辑配置不等于已写设备；读回一致也不证明固件持续采用该值。源码初值不等于当前RAM值。

## 证据

- `metadata.json`：变量、配置、ELF哈希、采样率与模拟标记。
- `samples.csv`：原始变量、主机时间、设备时间、epoch、stage和超期计数。
- `events.jsonl`：阶段变化、设备时间回退、参数写入、采集错误与停止。
- `summary.json`：样本数、时长、错误；不自动宣称业务结果通过。

按真实状态、时间和目标/实际分析，不按预期流程补齐缺失记录。未记录的分项不能根据名字猜测；残差不必是某一指定项。多字段非原子、短状态可能漏采，计数回退也可能是回绕，均要保留限制。

分别报告主机测试、编译、烧录、runtime与业务/物理结果。原始记录、本机配置、凭据和固件不提交中央源码仓库。TI案例中的具体判据见对应项目文档，不能套给其他工程。
