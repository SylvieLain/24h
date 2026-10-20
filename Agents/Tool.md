# TI 工具与验证

## 通用要求
- 先读取项目构建、调试和下载配置，确认实际芯片、SDK、编译器、启动文件、链接脚本、调试器与目标地址。旧工具路径、旧构建产物和配置选项不代表当前可用。
- 有 Git 时记录任务范围内的 status/diff；没有 Git 时保留内容副本或哈希清单。哈希只能证明是否变化，审查改动还需要前后内容；不要擅自初始化仓库。
- 新源文件要确认纳入实际构建列表，再检查入口/调用链是否使用它；写了文件不等于已编译或已经运行。
- 构建、烧录、寄存器/runtime、端到端链路、物理行为分别报告。只执行本任务适用检查，文档改动不要求重新烧录。
- 固件验收保留命令、工具版本、目标与原始输出。回读比对按实际程序地址和长度进行，不照抄其他系列芯片的地址。
- 烧录或测试涉及执行器时先确认本次硬件操作范围和所需状态；不默认执行整片擦除、解锁、恢复出厂或覆盖校准区。需要这些操作时另行明确授权。
- 不为一次调试引入通用测试平台、遥测框架或自动调参系统；复用已有工具，保留短而可复核的证据，不记录密码与密钥。

## 本项目工具入口（配置事实，未执行验证）
- EIDE构建/烧录链已按用户要求恢复：.eide/eide.yml uploader为JLink，MSPM0G3507/TI；.vscode的flash/buildAndFlash/syscfg为原EIDE入口，不由DAP接管。DAP只作为独立CLI/监控与external GDB attach使用，不擅自改IDE配置。
- 工程 `tools/OpenOCD` 只保留 readme.md（人类与AI）和 maintain.md。配置、Python环境、运行时与采集程序在中央 D:\CODE\mcu_tool；`dap --visualize` 打开参数滑块及任务时间轴。旧工具归档在中央 .state/ti-project-tool-archive-20261019-1957，原采集数据仍保留工程 work 下。
- build/Debug/builder.params 保存实际 sourceList；现有文件中的 toolchainLocation 是 d:/Tool-chain，后续构建前核对本机路径与生成时间。
- 链接输入：source/device_linker.lds、source/device.lds.genlibs；编译参数包含 source/device.opt。不得用 STM32 Flash 地址或旧下载脚本默认值推断本工程下载范围。
- SysConfig 入口有 tools/generate_syscfg.ps1（支持 -DryRun）和 .vscode/tasks.json 的 syscfg 任务。任务里含本机绝对工具/SDK 路径，换机器必须先核对；本次没有调用任何生成命令。
- 重新生成可能覆盖 source 下的配置与链接产物，先比较 .syscfg 与生成输出、保存基线。文件不一致时先解释差异，不直接用生成覆盖已有手改内容。
- tools/eide-sync、.eide-sync 保留现状，未检查或执行其同步工作流，不声称跨机器工具链可用。
