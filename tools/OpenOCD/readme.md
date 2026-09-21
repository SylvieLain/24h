# DAP 快速使用

只想读取数据或看实时曲线时读这一页。工具故障、换电脑或需要修改实现时再读 `maintain.md`。

在工程根目录启动唯一的 OpenOCD：

```powershell
Tool\OpenOCD\bin\dap.cmd server
```

保持该终端运行，另开终端执行以下命令。

```powershell
# 检查探针、目标运行状态和 ELF
Tool\OpenOCD\bin\dap.cmd doctor --json

# 搜索可读变量
Tool\OpenOCD\bin\dap.cmd symbols balance --json

# AI：一次读取
Tool\OpenOCD\bin\dap.cmd read balance.output,balance.kp_q10 --json

# 人类：实时曲线窗口
Tool\OpenOCD\bin\dap.cmd monitor balance.output balance.kp_q10

# AI：连续 5 秒 JSONL
Tool\OpenOCD\bin\dap.cmd monitor balance.output --json --duration 5

# 记录、画图、PID 指标
Tool\OpenOCD\bin\dap.cmd record balance.output encoder.left_counter --duration 10 --out run001.csv --json
Tool\OpenOCD\bin\dap.cmd draw run001.csv --out run001.png
Tool\OpenOCD\bin\dap.cmd pid analyze run001.csv --signal-column angle_x --output-column output --json

# 写白名单参数（dap.json writable/write_limits 限幅，写后读回验证，约 70ms）
Tool\OpenOCD\bin\dap.cmd write balance.kd_q10 512 --json

# 急停：写 balance.enabled=0（不要求 running，不等于物理断电）
Tool\OpenOCD\bin\dap.cmd estop --json

# 烧录当前 ELF：estop -> program verify -> reset run -> 自动恢复 server（实测约 2s）
Tool\OpenOCD\bin\dap.cmd flash --json
```

要点：

- `monitor` 默认是实时图形，不是纯数字窗口。
- 默认安全上限：1–4 个变量 50 Hz，5–8 个变量 25 Hz，更多变量 10 Hz。
- 读侧（read/monitor/record）只读不写；写侧仅限 `writable` 白名单并受 `write_limits` 限幅，双保险由固件限幅兜底。
- `flash` 会先尝试 estop，再关占用探针的 server，烧完自动恢复 server 并做活性自检。
- 当前 ELF 没有 `motor.speed`；先用 `symbols` 查真实符号，不能编造别名。
- 本工程目标是 TI MSPM0G3507：**必须用含 `mspm0` flash 驱动的 OpenOCD**。已随工程安装 `tools/openocd_xpack_0.12.0-7/`（xPack 0.12.0-7），`dap.json` 的 `openocd.executable/scripts` 已指向它。旧 `openocd_7a1adfbec`（0.12.0-rc2）无该驱动，会在 `flash bank ... mspm0` 处直接报错退出。
- `openocd.cfg` 固定使用 xpack 自带的 `[find target/ti_mspm0.cfg]`（与本 OpenOCD 版本匹配）。**不要改用工程根 `mspm0l.cfg`**：实测其在 `program`/`reset run` 后会把 CPU 留在 NMI，OpenOCD 报 `running` 但固件实际不执行（`Tick` 不递增）。
- `dap write` 同时支持整数与 float32：按 DWARF 类型编码，整数接受十进制与 `0x` 十六进制，浮点按相对容差读回；位域拒绝写入。
- 同一时间只能有一个 OpenOCD 占用探针。
- 报错、换路径、加别名或修改工具时再打开 `maintain.md`。
