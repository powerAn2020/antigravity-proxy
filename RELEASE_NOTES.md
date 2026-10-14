# Antigravity-Proxy v2.5.0 (Linux 首发版)

🎉 **Antigravity-Proxy 现已正式支持 Linux 原生平台！**

本项目基于 `LD_PRELOAD` 动态库拦截与 FakeIP 技术，为 Antigravity 系列应用（AI 开发环境 IDE 与 智能体桌面端 Client）以及 agy CLI 工具提供无需修改底层可执行文件的免 Root 透明代理网络重定向。

---

## 🚀 主要新特性与技术亮点

### 1. 原生 Linux 平台移植 (x86_64)
- **底层 POSIX Socket 符号劫持**：拦截 glibc 的 `connect`、`getaddrinfo`、`freeaddrinfo`、`send`、`recv`、`sendto`、`recvfrom`、`close`、`shutdown` 以及 `execve` 系列系统调用，实现对主流网络通信的无缝透明接管。
- **FakeIP 内存环形映射 (198.18.0.0/15)**：DNS 查询直接由动态库瞬时响应保留网段 FakeIP，实际请求时反向解析为真实域名并建立 SOCKS5 域名隧道，彻底解决 DNS 污染与解析延迟。
- **SOCKS5 TCP 与 UDP Associate 协议支持**：完整支持 TCP 隧道封装及 UDP Associate（用于 QUIC / HTTP3 等协议），具备 UDP 阻断回退 TCP 保护机制。

### 2. Chromium Seccomp-BPF 沙箱兼容
- 针对 Electron / Chromium 渲染器（`--type=renderer`）、GPU 进程（`--type=gpu-process`）、Zygote 进程与 Crashpad 处理器精准识别。
- 沙箱子进程完全豁免 `LD_PRELOAD` 符号拦截，彻底解决 Chromium 进程在 Linux 沙箱下的 `seccomp-bpf failure in syscall nr=0x37 (code: 11)` 崩溃问题。
- 启动器原生传递 `--proxy-server` 与 `--proxy-bypass-list` 命令行参数，确保 Chromium 内部网络进程（Network Service）正常走代理。

### 3. 三层立体代理方案（解决 Go 语言服务器网络穿透）
- 针对 Antigravity 核心 AI 代码补全服务 `language_server_linux_x64`（Go 编译、直调内核汇编系统调用绕过 glibc 的特性），动态库与启动脚本在派生子进程时自动自愈注入 `ALL_PROXY`、`all_proxy`、`HTTP_PROXY`、`http_proxy`、`HTTPS_PROXY`、`https_proxy`、`NO_PROXY`。
- 无论通过脚本、CLI 启动，还是由主程序清洗环境后派生，语言服务器均能无缝通过用户代理访问 Google 云端大模型接口。

### 4. 双应用智能解耦与桌面集成
- **独立支持 IDE 与桌面端客户端**：
  - `antigravity-ide.sh`：专用于启动 **Antigravity IDE**（代码编辑器/开发环境）。
  - `antigravity.sh`：专用于启动 **Antigravity**（智能体桌面端客户端）。
- **智能双桌面项生成**：`patch-desktop.sh` 及 CLI `patch-desktop` 子命令可独立扫描系统中安装的应用，自动提取原生高分图标，在 Linux 应用菜单中生成互不干扰的双启动项：
  - `Antigravity IDE (Proxied)`（StartupWMClass: `antigravity-ide`）
  - `Antigravity (Proxied)`（StartupWMClass: `antigravity`）
- **环境变量灵活支持**：支持 `ANTIGRAVITY_IDE_PATH` 与 `ANTIGRAVITY_PATH`，若应用安装在非标准目录，可通过环境变量自由指定。

### 5. 专属 Linux CLI 管理工具
- 提供全功能 `antigravity-proxy` 管理工具（支持 `status` 环境与路径诊断、`test` 代理连通性测试、`ide` 一键启动 IDE、`app` 一键启动桌面客户端、`agy` 包装运行 CLI、`run` 包装运行任意系统命令）。
- 编译脚本增加用户配置持久化保护，避免二次构建覆盖用户代理端口与分流规则。

---

## 🧪 测试环境与验证报告

本版本在真实 Linux 桌面环境中完成全套自动化测试与实机功能验证：

| 测试项 | 详细环境与配置 |
|---|---|
| **操作系统** | Zorin OS 18.1 (基于 Ubuntu 24.04 LTS / Noble 64位) |
| **Linux 内核** | `Linux 7.0.0-38-generic #38~24.04.4-Ubuntu SMP PREEMPT_DYNAMIC x86_64` |
| **C 运行库** | GNU C Library (glibc) 2.39 |
| **编译器与构建链** | GCC 13.3.0, CMake 3.28.3, Ninja 1.11.1 |
| **图形显示服务** | Wayland / X11 (GNOME Desktop) |
| **测试目标 1 (IDE)** | Antigravity IDE (`~/Applications/Antigravity IDE/antigravity-ide`) |
| **测试目标 2 (客户端)** | Antigravity Client (`~/Applications/Antigravity-x64/antigravity`) |
| **测试代理服务端** | Xray Core 1.8+ / Mixed 混合协议 (HTTP + SOCKS5) / 本地端口 10808 |

### 验证结论
- **CTest 单元测试套件**：`6/6` 全部通过（通过率 100%）。
  - `antigravity_tests`：Passed
  - `process_name_tests`：Passed
  - `udp_policy_tests`：Passed
  - `socket_family_tests`：Passed
  - `local_target_policy_tests`：Passed
  - `linux_hooks_tests`：Passed
- **Python E2E 端到端回归测试**：`4/4` 全部通过（通过率 100%）。
  - `Test 1`：TCP SOCKS5 透明拦截与 FakeIP 域名回溯验证通过
  - `Test 2`：SOCKS5 UDP Associate `sendto` / `recvfrom` 转发验证通过
  - `Test 3`：SOCKS5 UDP Associate `connect` / `send` / `recv` 转发验证通过
  - `Test 4`：Go 目标进程（`language_server`）代理环境变量自动注入验证通过
- **实网透明代理验证**：通过 `./output/ide/antigravity-proxy run curl -s https://httpbin.org/ip` 成功通过本地代理获取外部出口 IP `104.28.214.122`，耗时 1.8 秒，连接稳定。
- **桌面快捷方式**：已成功在系统桌面应用列表中生成 `Antigravity IDE (Proxied)` 与 `Antigravity (Proxied)` 启动项并验证正常加载。

---

## 📦 发布产物与校验信息 (Assets & Checksums)

| 文件名 | 类型 | 说明 | SHA256 校验码 |
|---|---|---|---|
| `antigravity-proxy-v2.5.0-ide-linux-x64.tar.gz` | IDE 产物包 (tar.gz) | 包含核心动态库、双应用启动脚本、双桌面项注入脚本、配置工具 | `5bbefc492fe7c1403bf7b4fff06d56fd91edb5eee1756a4f004b1014d13a3062` |
| `antigravity-proxy-v2.5.0-ide-linux-x64.zip` | IDE 产物包 (zip) | 同上（zip 格式） | `1c5528b2b54cb46f5ae733d171de8ec9aea4742a049c33ce44324c58f3cc2d3d` |
| `antigravity-proxy-v2.5.0-cli-linux-x64.tar.gz` | CLI 产物包 (tar.gz) | 包含核心动态库、antigravity-proxy 命令行管理工具、agy 包装脚本 | `0ff1b2403722d8ff40535776c5620632b24cce176de9f49edf3f61014ffc4b0d` |
| `antigravity-proxy-v2.5.0-cli-linux-x64.zip` | CLI 产物包 (zip) | 同上（zip 格式） | `ce75f519ea39fbad3d5927f7ffdcec65a88c7f5bb4261c66d5d6aa6c5383839a` |

---

## 🛠️ 快速上手与非标准路径说明

### 1. IDE 与客户端用户
1. 下载解压 `antigravity-proxy-v2.5.0-ide-linux-x64.tar.gz`；
2. 修改 `config.json` 中的 `proxy.port` 为您的本地代理客户端端口（默认 7890，如使用 Xray/v2rayN/Clash 可改为对应本地端口，如 10808）；
3. **启动应用**：
   - 启动 IDE：运行 `./antigravity-ide.sh`
   - 启动智能体桌面端：运行 `./antigravity.sh`
4. **生成桌面应用菜单图标**：
   - 运行 `./patch-desktop.sh`，系统将自动识别并分别生成 `Antigravity IDE (Proxied)` 与 `Antigravity (Proxied)` 启动图标。

### 2. 非标准路径指定
如果应用未安装在系统 PATH 或默认 Applications 目录中，可通过环境变量指定：
```bash
# 单次命令行启动
ANTIGRAVITY_IDE_PATH="/path/to/antigravity-ide" ./antigravity-ide.sh
ANTIGRAVITY_PATH="/path/to/antigravity" ./antigravity.sh

# 生成指定路径的桌面菜单图标
ANTIGRAVITY_IDE_PATH="/path/to/antigravity-ide" \
ANTIGRAVITY_PATH="/path/to/antigravity" \
./patch-desktop.sh

# 或永久添加到 ~/.bashrc
export ANTIGRAVITY_IDE_PATH="/path/to/antigravity-ide"
export ANTIGRAVITY_PATH="/path/to/antigravity"
```

### 3. CLI 用户
1. 下载解压 `antigravity-proxy-v2.5.0-cli-linux-x64.tar.gz`；
2. 运行 `./antigravity-proxy test` 测试代理连通性；
3. 运行 `./agy.sh` 或 `./antigravity-proxy agy` 即可带代理使用 agy 工具。
