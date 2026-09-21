# CCS MCP 接入说明

本文档描述 TI Code Composer Studio（CCS）Theia 20.5.0.00028 内置 MCP 服务，以及在 OpenCode 中接入这三个服务的配置方式与手动核验步骤。

---

## 1. 这是什么

CCS 20.5 内置了 **三个** MCP（Model Context Protocol）服务，均随 CCS 一起安装、随 CCS 启动而拉起：

| 服务名 | 作用域 | 本地地址 |
| --- | --- | --- |
| `project` | 工程管理、构建、产物与编译器查询 | `http://127.0.0.1:61980/mcp/project` |
| `debug` | 目标连接、下载、内存、断点、表达式求值 | `http://127.0.0.1:61980/mcp/debug` |
| `sysconfig` | SysConfig 配置文件的模块/实例修改与代码生成 | `http://127.0.0.1:55089/mcp` |

> 注意：`project` 与 `debug` 共用 61980 端口，路径不同；`sysconfig` 独立占用 55089 端口。

---

## 2. 接入方式（OpenCode 本地 MCP）

在全局配置 `C:\Users\蛟龙\.config\opencode\opencode.jsonc` 的 `mcp` 对象中新增三个 `local` 服务器，`matlab` 条目保持不变：

```jsonc
"mcp": {
  "matlab": {
    // ……保持原样，未改动……
  },
  "ccs-project": {
    "type": "local",
    "command": [
      "D:\\电子信息\\ccs\\tools\\node\\node.exe",
      "D:\\电子信息\\ccs\\theia\\resources\\app\\plugins\\ccs-ai\\extension\\dist\\mcp-server-proxy.js",
      "project"
    ]
  },
  "ccs-sysconfig": {
    "type": "local",
    "command": [
      "D:\\电子信息\\ccs\\tools\\node\\node.exe",
      "D:\\电子信息\\ccs\\theia\\resources\\app\\plugins\\ccs-ai\\extension\\dist\\mcp-server-proxy.js",
      "sysconfig"
    ]
  },
  "ccs-debug": {
    "type": "local",
    "command": [
      "D:\\电子信息\\ccs\\tools\\node\\node.exe",
      "D:\\电子信息\\ccs\\theia\\resources\\app\\plugins\\ccs-ai\\extension\\dist\\mcp-server-proxy.js",
      "debug"
    ]
  }
}
```

### 命令与参数说明

- **第 1 个参数**：`node.exe`，CCS 自带的 Node 运行时，避免依赖系统 Node 版本。
  `D:\电子信息\ccs\tools\node\node.exe`
- **第 2 个参数**：CCS 提供的 stdio 代理脚本。
  `D:\电子信息\ccs\theia\resources\app\plugins\ccs-ai\extension\dist\mcp-server-proxy.js`
- **第 3 个参数（必须）**：服务器名，取值为 `project` / `sysconfig` / `debug` 之一。

> **重要**：代理脚本会从 `argv[2]` 读取服务器名，并校验其是否属于 `["debug","project","sysconfig"]`。**官方 README 漏写了这个位置参数**，因此启动命令必须显式携带该名字，否则代理无法定位目标服务。三个条目分别填 `project`、`sysconfig`、`debug`。

代理的工作方式：它启动后读取 CCS 运行时登记的端口信息，再用 stdio <-> HTTP 的方式把 OpenCode 与本机 CCS MCP HTTP 端点桥接起来。因此 OpenCode 侧只配置本地 stdio 命令即可，无需直接写 HTTP URL。

---

## 3. 前置条件

1. **CCS 必须处于运行状态**。三个 MCP 服务由 CCS 进程托管，CCS 未启动时端口不存在，代理会连接失败。
2. **端口由 CCS 动态分配**，并非固定写死。CCS 启动后会把实际监听地址写入运行时登记文件：
   - 目录：`C:\Windows\TEMP\ccs-ai\`
   - 文件名形如：`ccs-ai-port-<hash>`
   - 内容为 JSON，其中按服务器列出对应的 `serverUrl` 条目（`project` / `debug` / `sysconfig`）。
3. 上文出现的 `61980` / `55089` 是当前会话的实测端口，**仅供参考**；每次以登记文件中的实际值为准。

---

## 4. 三个服务的关键工具与用途

### 4.1 `project`（工程 / 构建）

| 工具 | 用途 |
| --- | --- |
| `buildProject` | **构建工程**。这是唯一的构建入口，不要绕过它手工调用编译器。 |
| `importProject` | 导入已有工程到 CCS 工作区。 |
| `getProducts` | 查询已安装的 CCS 产品/组件。 |
| `getCompilers` | 查询可用编译器及其版本。 |
| `changeSysConfigVersion` | 切换工程所用的 SysConfig 版本。 |
| `getProjectDescriptors` | （只读）列出工程描述符，适合作为接入后的首个验证调用。 |

### 4.2 `sysconfig`（图形化配置 / 代码生成）

| 工具 | 用途 |
| --- | --- |
| `openFile` | 在 SysConfig 中打开指定 `.syscfg` 文件。 |
| `listModules` | （只读）列出当前配置中的模块，适合作为首个验证调用。 |
| `addModuleInstances` | 添加模块实例。 |
| `changeConfiguration` | 修改配置项。 |
| `generateArtifacts` | **根据 `.syscfg` 生成产物**（生成的头/源文件等）。改完配置后必须调用它，让生成物落到工程里。 |
| `readGeneratedArtifact` | 读取已生成的产物内容。 |
| `save` | 保存当前 SysConfig 配置。 |

### 4.3 `debug`（调试 / 目标）

| 工具 | 用途 |
| --- | --- |
| `connectTarget` | 连接调试目标。 |
| `loadProgram` | 下载程序到目标。 |
| `readMemory` / `writeMemory` | 读 / 写目标内存。 |
| 断点相关工具 | 设置 / 管理断点。 |
| `evaluate` | 求值表达式。 |

> 调试类操作会实际触及硬件。在没有明确指令前，仅做只读核验，不要下载或擦写。

---

## 5. 手动核验步骤（由用户自行执行）

**a. 确认登记文件存在且含 project/sysconfig URL**

CCS 启动后：

```powershell
Get-ChildItem "C:\Windows\TEMP\ccs-ai\" -Filter "ccs-ai-port-*"
Get-Content "C:\Windows\TEMP\ccs-ai\ccs-ai-port-<hash>" | ConvertFrom-Json
```

确认输出中存在 `project` 与 `sysconfig` 的 `serverUrl` 条目。

**b. 直接打 MCP HTTP 端点**

对 project 服务发送 MCP `initialize` + `tools/list`，期望 HTTP 200：

```powershell
$body = '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"probe","version":"0"}}}'
Invoke-WebRequest -Uri "http://127.0.0.1:61980/mcp/project" -Method Post -ContentType "application/json" -Body $body
```

**c. 重启 OpenCode 后确认工具已注册**

重启 OpenCode，确认 MCP 列表中能看到 `ccs-project`、`ccs-sysconfig`、`ccs-debug` 三个服务器，并能列出各自工具。

**d. 先只读、后写入**

先用只读工具验证链路是否真正可用，例如：
- `project` 的 `getProjectDescriptors`
- `sysconfig` 的 `listModules`

确认只读调用正常后，再执行 `buildProject`、`generateArtifacts` 等写操作。

---

## 6. 【待验证】清单

以下事项尚未实跑确认，使用前需自行验证：

- **代理是否必须显式传服务器名**：根据代理源码，服务器名从 `argv[2]` 读取并做白名单校验，**判断为必须**；但未实际以「缺失该参数」的方式运行对比，属源码推断。
- **sysconfig 服务当前会话可能是 `project: empty`**：若未在 CCS 中打开任何工程或 `.syscfg`，`sysconfig` 的文件类工具（`openFile`、`listModules`、`generateArtifacts` 等）可能无目标可选。是否需要先在 CCS 中打开某个 `.syscfg` 文件，需实测确认。
- **`openocd-mcp.json` 与本 CCS MCP 无关**：该文件不属于本接入链路，不要把它与本文档的三个服务混淆。

---

## 7. 边界与注意事项

- **不要手改 `.syscfg` / `.project` / `.cproject`**：这些文件由工具托管。需要改配置时，走 `sysconfig` MCP 的相关工具。
- **构建走 `buildProject`**：不要绕过 `project` MCP 直接手工调用编译/链接命令。
- 文档只描述接入与使用方法；具体的工程路径、目标芯片、调参策略不在此处约定。
