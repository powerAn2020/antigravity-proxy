<p align="center">
  <img src="img/antigravity_logo.png" alt="Antigravity Logo" width="120"/>
</p>

<h1 align="center">Antigravity-Proxy</h1>

<p align="center">
  <b>🚀 专为 Antigravity 系列应用打造的原生免 TUN 透明代理重定向工具 (Linux / Windows)</b>
</p>

<p align="center">
  <a href="LICENSE.txt"><img src="https://img.shields.io/badge/license-BSD--2--Clause-blue.svg" alt="License"/></a>
  <img src="https://img.shields.io/badge/platform-Linux%20%7C%20Windows-lightgrey.svg" alt="Platform"/>
  <img src="https://img.shields.io/badge/C%2B%2B-17-00599C.svg" alt="C++17"/>
  <img src="https://img.shields.io/badge/Linux-LD__PRELOAD-success.svg" alt="Linux LD_PRELOAD"/>
  <img src="https://img.shields.io/badge/arch-x86__64-orange.svg" alt="Architecture"/>
</p>

---

## 📖 项目介绍 / Introduction

**Antigravity-Proxy** 是专为 **Antigravity 系列应用**（Antigravity IDE 开发环境、Antigravity 智能体桌面端、agy CLI）量身定制的高性能透明代理网络重定向套件。

它的核心目标：**在中国大陆等复杂网络环境下，无需开启 Clash / VPN 的全局 TUN 虚拟网卡模式，无需管理员或 Root 权限，即可让 Antigravity 稳定透明地通过本地 SOCKS5 / HTTP 代理访问云端大模型服务。**

### 🎯 解决的核心痛点
- 🔴 **免全局接管**：普通 TUN 虚拟网卡模式会全局接管整台主机的网络流量，不仅影响本地开发、内网穿透与国内站点速度，且需要系统高权限。
- 🔴 **穿透 Go 语言底层服务**：Antigravity IDE 内部的核心代码补全语言服务器（`language_server_linux_x64`）由 Go 语言静态编译，默认忽略系统代理甚至直调内核系统调用绕过 glibc，常规代理工具对其无效。
- 🟢 **Antigravity-Proxy 的立体代理方案**：
  - **Linux 平台**：基于 `LD_PRELOAD` POSIX Socket 符号劫持 + FakeIP 内存虚拟网段（`198.18.0.0/15`）快速响应 + 进程树环境自愈注入，实现**免 Root、无感知、精准靶向**的透明重定向。
  - **Windows 平台**：基于 `version.dll` / `dbghelp.dll` 的 MinHook 劫持技术。

---

# Antigravity-Proxy v2.5.0 (Linux 原生首发版)

🎉 **Antigravity-Proxy 现已正式支持 Linux 原生平台！**

本项目通过动态库网络劫持与 FakeIP 域名回溯技术，为 Linux 下的 Antigravity 系列应用（AI 开发环境 IDE 与 智能体桌面端 Client）以及 agy CLI 工具提供无需修改底层可执行文件的免 Root 透明代理网络重定向。

---

## 🚀 主要新特性与技术亮点

### 1. 原生 Linux 平台移植 (x86_64)
- **底层 POSIX Socket 符号劫持**：拦截 glibc 的 `connect`、`getaddrinfo`、`freeaddrinfo`、`send`、`recv`、`sendto`、`recvfrom`、`close`、`shutdown` 以及 `execve` 系列系统调用，实现对主流网络通信的无缝透明接管。
- **FakeIP 内存环形映射 (198.18.0.0/15)**：DNS 查询直接由动态库瞬时响应保留网段 FakeIP，实际请求时反向解析为真实域名并建立 SOCKS5 域名隧道，彻底解决 DNS 污染与解析延迟。
- **SOCKS5 TCP 与 UDP Associate 协议支持**：完整支持 TCP 隧道封装及 UDP Associate（用于 QUIC / HTTP3 等协议），具备 UDP 阻断回退 TCP 保护机制。

### 2. Chromium Seccomp-BPF 沙箱兼容
- 精准识别 Electron / Chromium 渲染器（`--type=renderer`）、GPU 进程（`--type=gpu-process`）、Zygote 进程与 Crashpad 处理器。
- 沙箱子进程完全豁免 `LD_PRELOAD` 符号拦截，彻底解决 Chromium 进程在 Linux 沙箱下的 `seccomp-bpf failure in syscall nr=0x37 (code: 11)` 崩溃问题。
- 启动器原生传递 `--proxy-server` 与 `--proxy-bypass-list` 命令行参数，确保 Chromium 内部网络进程（Network Service）正常走代理。

### 3. 三层立体代理方案（解决 Go 语言服务器网络穿透）
- 针对 Antigravity 核心 AI 代码补全服务 `language_server_linux_x64`（Go 编译、直调内核汇编系统调用绕过 glibc 的特性），动态库与启动脚本在派生子进程时自动自愈注入代理环境变量（`ALL_PROXY`、`HTTP_PROXY`、`HTTPS_PROXY` 及小写别名）。
- 无论通过脚本、CLI 启动，还是由主程序清洗环境后派生，语言服务器均能无缝通过用户代理访问 Google 云端大模型接口。

### 4. 双应用智能解耦与桌面集成
- **独立分工双脚本**：
  - `antigravity-ide.sh`：专用于启动 **Antigravity IDE**（代码编辑器/开发环境）。
  - `antigravity.sh`：专用于启动 **Antigravity**（智能体桌面端客户端）。
- **智能双桌面项生成**：`patch-desktop.sh` 及 CLI `patch-desktop` 子命令可独立扫描系统中安装的应用，自动提取高分图标，在 Linux 应用启动器中生成互不干扰的双启动项：
  - `Antigravity IDE (Proxied)`（StartupWMClass: `antigravity-ide`）
  - `Antigravity (Proxied)`（StartupWMClass: `antigravity`）
- **非标准路径环境变量支持**：内置支持 `ANTIGRAVITY_IDE_PATH` 与 `ANTIGRAVITY_PATH`，安装在自定义目录时无需改动脚本即可启动或生成桌面项。

### 5. 专属 Linux CLI 管理工具
- 提供全功能 `antigravity-proxy` 管理工具（支持 `status` 诊断、`test` 代理连通性测试、`ide` 启动 IDE、`app` 启动桌面端客户端、`agy` 包装运行 CLI、`run` 包装运行任意系统命令）。
- 编译脚本增加用户已有配置持久化保护，避免二次构建覆盖用户代理端口与分流规则。

---

## 🧪 真实测试环境与验证报告

本版本在真实 Linux 桌面环境中完成全套自动化测试与实机功能验证：

| 测试项 | 详细环境与配置 |
|---|---|
| **操作系统** | Zorin OS 18.1 (基于 Ubuntu 24.04 LTS / Noble 64位) |
| **Linux 内核** | `Linux 7.0.0-38-generic #38~24.04.4-Ubuntu SMP PREEMPT_DYNAMIC x86_64` |
| **C 运行库** | GNU C Library (glibc) 2.39 |
| **编译器与构建链** | GCC 13.3.0, CMake 3.28.3, Ninja 1.11.1 |
| **图形显示服务** | Wayland / X11 (GNOME Desktop) |
| **实测目标 1 (IDE)** | Antigravity IDE (`~/Applications/Antigravity IDE/antigravity-ide`) |
| **实测目标 2 (客户端)** | Antigravity Client (`~/Applications/Antigravity-x64/antigravity`) |
| **本地代理服务端** | Xray Core 1.8+ / Mixed 混合协议 (HTTP + SOCKS5) / 本地端口 10808 |

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
- **实网透明代理验证**：通过 `./antigravity-proxy run curl -s https://httpbin.org/ip` 成功通过本地代理获取外部出口 IP `104.28.214.122`，连接稳定。
- **桌面快捷方式**：系统应用列表中已成功分别生成 `Antigravity IDE (Proxied)` 与 `Antigravity (Proxied)` 启动项并验证各自独立加载。

---

## 🛠️ 快速上手与使用指南

### 1. IDE 与客户端用户
1. 从 GitHub Releases 下载解压 `antigravity-proxy-v2.5.0-ide-linux-x64.tar.gz`（或 `.zip`）；
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
1. 从 Releases 下载解压 `antigravity-proxy-v2.5.0-cli-linux-x64.tar.gz`；
2. 运行 `./antigravity-proxy test` 测试代理连通性；
3. 运行 `./agy.sh` 或 `./antigravity-proxy agy` 即可带代理使用 agy 工具。

---

## 🏗️ 源码编译与构建 (Linux)

```bash
# 1. 安装编译工具 (Debian / Ubuntu 系)
sudo apt update && sudo apt install -y build-essential cmake ninja-build

# 2. 编译并打包
./build.sh

# 3. 运行完整自动化测试
ctest --test-dir build_linux --output-on-failure
python3 tests/test_e2e_proxy.py
```

产物将自动生成于 `output/ide` 与 `output/cli`。

---

## 🙏 致谢上游开发者 / Acknowledgements

本项目 Linux 移植版深深感谢原项目上游开发者 **[@yuaotian](https://github.com/yuaotian)**（煎饼果子 86）及其开源的 Windows 版 [yuaotian/antigravity-proxy](https://github.com/yuaotian/antigravity-proxy)。

原项目在 Windows 平台通过 MinHook 奠定了优雅而扎实的 FakeIP 与动态代理核心架构，为本次 Linux 平台的完整重构移植提供了至关重要的灵感、算法参考与架构基石。特此致以诚挚的敬意与感谢！

---

## 📄 许可证 / License

本项目基于 [BSD 2-Clause License](LICENSE.txt) 开源。
