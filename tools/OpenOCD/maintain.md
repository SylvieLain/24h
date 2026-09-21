# DAP 维护说明

仅在安装、故障诊断、换探针/电脑、修改 CLI 或扩展采样结构时读取。日常使用只读 `readme.md`。

## 目录职责

```text
Tool/OpenOCD/
  readme.md / maintain.md      快速使用 / 维护说明
  openocd.cfg                  探针、串号、SWD 速度、目标
  dap.json                     CLI、ELF、别名、安全频率
  openocd-mcp.json             MCP 权限和目标配置
  bin/                         Windows/POSIX CLI 入口
  src/dap_cli/                 CLI 实现
  requirements/                Python 依赖及锁定版本
  scripts/                     独立 OpenOCD/调试窗口脚本
  analysis/                    历史专项分析脚本
  plots/                       生成图
  .venv/ .state/               本机环境和运行状态，不入 Git
```

根目录不再放临时测试脚本、历史分析程序或生成图。

## 安装

Windows：

```powershell
py -3 -m venv Tool\OpenOCD\.venv
Tool\OpenOCD\.venv\Scripts\python.exe -m pip install -r Tool\OpenOCD\requirements\requirements-mcp.txt
```

Linux/macOS：

```sh
python3 -m venv Tool/OpenOCD/.venv
Tool/OpenOCD/.venv/bin/python -m pip install -r Tool/OpenOCD/requirements/requirements-mcp.txt
sh Tool/OpenOCD/bin/dap server
```

CLI 源码跨平台；换机器时修改 `dap.json` 中 OpenOCD executable、scripts 和 ELF 路径。

## 配置修改

- 探针串号、SWD 速度、芯片目标：修改 `openocd.cfg`。
- ELF、Tcl 端口、别名、频率保护：修改 `dap.json`。
- MCP 工具权限：修改 `openocd-mcp.json` 和工程 `.vscode/mcp.json`。
- 别名右侧必须是真实 ELF 全局变量或 `global_struct.member`，不得把编码器原始计数伪装成速度。

诊断顺序：

```powershell
Tool\OpenOCD\bin\dap.cmd doctor --json
Tool\OpenOCD\bin\dap.cmd symbols 关键词 --json
Tool\OpenOCD\bin\dap.cmd read 真实变量 --json
```

`doctor` 失败先处理供电、探针占用、串号、6666 端口和 ELF 路径，不先改采样代码。它还会只读采样两次 `dap.json` 的 `liveness_symbol`；目标虽为 running 但该值不递增时，应优先排查 ELF/板上固件不匹配或目标时基停滞。

## 实现约束

`read/monitor/record` 只允许使用 `targets` 和 `mdb`。目标不是 `running` 时直接失败，禁止自动改变目标状态。

`dap write` 现在同时支持整数与 float32 变量：按 DWARF 类型编码，整数接受十进制和 `0x` 十六进制，浮点按 4 字节小端打包并按相对容差读回比对。位域（bitfield）拒绝写入，不做读改写，避免破坏相邻位。

采样线程与图形线程分离：

```text
OpenOCD Tcl -> 采样线程 -> 有界队列 -> 实时曲线
                                  -> JSONL
                                  -> CSV
```

相邻符号自动合并为最大 256 字节的连续读取块，减少无线往返。GUI 默认 30 FPS；GUI 变慢只能丢显示帧，不能阻塞采样。

不要在 MCU ISR/控制路径进行 JSON、CSV、字符串格式化、绘图或发送。200 Hz 控制数据应由 C 端按周期写连续快照/环形缓冲区，PC 每 20–50 ms 批量读取并按 MCU 时间戳还原。

建议快照至少包含：

```text
seq_begin, target_tick, angle, gyro, speed, output,
control_deadline_miss_count, control_max_late_us,
trace_dropped_count, seq_end
```

读取前后序号不一致则丢弃该快照，避免读取到更新一半的数据。

## 当前链路实测边界

测试条件：无线 CMSIS-DAP v2、串号 `183b2401`、SWD 1 MHz、STM32F103。

| 读取方式 | 实测 |
|---|---:|
| 单个 32 位变量 | 约 352–369 次/秒 |
| 4 个分散变量逐个读取 | 约 87 组/秒 |
| 8 个分散变量逐个读取 | 约 44 组/秒 |
| 连续 64 字节 | 约 102 块/秒 |
| 连续 256 字节 | 约 30 块/秒，约 7.7 KB/s |
| 当前 CLI 四变量合并读取 | 约 6–7 ms/组，50 Hz 无超期 |

超过 `dap.json` 的安全频率会拒绝执行；`--force` 只用于受控压力测试。

## “不影响程序”的验收

运行中 SWD 读 RAM 不是物理上的绝对零扰动。必须区分：

- 已通过：目标保持 running、毫秒时基连续、50 Hz 主机采样无超期。
- 未证明：闭环开启后 5 ms 控制周期没有微秒级抖动。

在轮子安全架起且闭环实际开启时，对比各 30 秒：不采样、默认频率采样、连续块采样。检查 `control_seq`、deadline miss、最大延迟和丢样计数；不要用“窗口有数据”代替闭环无扰验收。

## 修改后的最小回归

```powershell
Tool\OpenOCD\.venv\Scripts\python.exe -m py_compile Tool\OpenOCD\bin\dap.py Tool\OpenOCD\src\dap_cli\cli.py
Tool\OpenOCD\bin\dap.cmd --help
Tool\OpenOCD\bin\dap.cmd doctor --json
Tool\OpenOCD\bin\dap.cmd read balance.output --json
Tool\OpenOCD\bin\dap.cmd monitor balance.output --json --duration 1
```

图形相关修改还必须实际打开一次 `monitor` 窗口；CSV/PID 修改必须用一份已知数据运行 `record`、`draw` 和 `pid analyze`。

验证时记录 ELF SHA-256。`doctor` 能报告主机 ELF 哈希，但在固件提供构建身份字段前，不能自动证明板上程序与该 ELF 完全一致。
