# DAP 维护与工程入口

## 中央目录

源码/CLI/界面为 `bin/` 与 `src/dap_cli/`；项目配置在忽略Git的 `projects.local.json`、`profiles/*.local.json/.cfg`；运行数据在 `runs/`；迁移备份在 `.state/`；可选本机 OpenOCD 运行时在 `.runtime/`。这些本机内容和凭据均不提交。

TI工程 `tools/OpenOCD` 只留 `readme.md` 与 `maintain.md`，不留脚本、JSON、旧虚拟环境或图表。它通过全局PATH和IDE配置使用中央仓库内容，而不是目录联接、源码副本或另一套Python环境。

原工具已完整归档到 `.state/ti-project-tool-archive-20261019-1957`。旧采集器在用户授权后通过其STOP标记正常结束，原始CSV/事件/摘要仍留在工程work目录。原OpenOCD服务未操作；其启动命令显示旧cfg路径是已加载的启动历史，不表示以后仍应使用旧目录启动。

## 更新与配置

更新中央仓库代码后，重新启动监控面板Python服务才能使用新后端；浏览器刷新可更新JS/CSS。`--visualize` 是 `dashboard --open` 的等价入口，打开页面本身不接设备。

`profiles/ti-revenge.dashboard.json` 是可移植的界面/公式样例；将其 `dashboard/loops` 内容按实际固件核对后合入自己的local配置。UI添加/编辑条目会保存分组、角色、滑块边界和步长；可写参数会加入白名单并记录限值。删除界面条目不静默撤销已有白名单，维护者应按实际需要核对白名单。

角色 `gain/target` 可写，`actual/watch/state` 只读。所有状态位均显示在公共状态时间轴；分组筛选控制参数和普通观测。新条目默认分组1，1不能被移除。

项目选择顺序：显式参数 > 当前工作目录所在工程 > `project use`记忆 > 注册表默认。切换项目不自动切换OpenOCD。每项目应有匹配的ELF、端口、精确目标名称、RAM范围和OpenOCD配置。

## IDE配置

- `.eide/eide.yml`：按用户要求恢复原 `uploader: JLink`，器件MSPM0G3507、厂商TI。F9不由DAP接管。
- `.vscode/tasks.json`：flash/build and flash恢复原EIDE命令，syscfg任务恢复。中央DAP通过独立终端命令使用。
- `9.5.code-workspace`：Cortex-Debug `servertype: external`, `request: attach`, `gdbTarget: localhost:3333`, `loadFiles: []`；不自动烧录或运行到main。

`.vscode`与workspace文件按现有工程gitignore被忽略。此前替换EIDE上传器超出了用户期望，已恢复；保留独立DAP调试配置不代表可重写IDE烧录链。历史证据和第三方OpenOCD通用配置不删除。

TI官方 `target/ti_mspm0.cfg` 的examine-start包含对PWR_AP::SPREC的SYSRST写入，OpenOCD重新连接可能复位MCU。不要把“未发送reset命令”说成“没有发生复位”。

## 维护验证

```powershell
cd D:\CODE\mcu_tool
.venv\Scripts\python.exe -m unittest discover -s tests -v
node --check src/dap_cli/dashboard.js
dap --project ti-revenge project show
dap --project ti-revenge --visualize --demo
```

离线验证不运行doctor或读真实6666。图形变更需实际验证滑块暂存、开始/停止、状态/目标时间对照、条目分组编辑和回放；采集逻辑检查Tick回退、错误保留、磁盘文件与模拟标志。

本次两个固件补丁只生成且可应用检查通过，没有应用、编译或烧录。`heading-pd-terms.patch`为PID结构末尾增加3个float并保存分项，需全量重编译；旧结构成员偏移保持不变，新字段需要新ELF。仍非一致快照。TASK34补丁独立，不能当航向PD参数方案。
