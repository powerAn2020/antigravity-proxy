---
task:   Linux平台移植补全方案
date:   2026-10-14
type:   新功能
level:  完整
status: 已完成
chosen: 方案A（模块复用桥接+隔离式更新检测+全链路测试）
---

# Linux平台移植补全方案

## 方案逻辑通俗说明（开篇必读，落盘必填）

- **问题是什么**：
  本项目最初专为 Windows 平台打造，虽然在此前提交中初步搭建了 Linux 基础（通过 `LD_PRELOAD` 劫持 `connect` 实现 HTTP/TCP 代理），但核心的 **UDP 代理中继（SOCKS5 UDP Associate）完全未接管**，导致 Linux/WSL 环境下诸如 QUIC、HTTP/3 等基于 UDP 的请求要么直连泄漏、要么直接报错阻断；同时 Linux 平台下的后台更新检查目前也是空桩，尚未达到与 Windows 版本等价的功能完备度。

- **方案怎么解决它**：
  1. **UDP 代理桥接**：当 Linux 程序在 UDP 套接字上调用 `connect`、`sendto` 或 `send` 时，代理动态库拦截这些调用，先向配置好的 SOCKS5 代理建立一条轻量 TCP 控制通道完成握手，拿到代理分配的 UDP 中继端口（Relay）；随后把程序发送的原始 UDP 数据打包加上 SOCKS5 协议头，投递给代理中继；当代理中继回包时，通过拦截 `recvfrom` 与 `recv`，剥掉 SOCKS5 协议头，将真实的原始响应交给程序。整个过程应用完全无感。
  2. **非侵入式后台更新检查**：为防止在 `LD_PRELOAD` 动态库中引入重量级的 OpenSSL/libcurl 导致与目标程序（如 Node.js 或 Electron 编辑器）发生动态符号冲突（ABI 崩溃），Linux 端在后台线程通过系统标准命令行工具拉取 GitHub Release 信息，做到零外部依赖。
  3. **自动化测试覆盖**：编写本地 Mock SOCKS5 UDP 服务端与集成测试用例，确保持续集成和构建部署在 Linux 下均可自动化一键验证。

- **为什么这样就能解决**：
  复用了现有经充分检验的 RFC 1928 `Socks5Udp` 协议封包模块，既保留了单点维护的极简性，又通过 POSIX 标准拦截钩子实现了与 Windows 等价的透明代理机制，保证了在 WSL 与原生 Linux 环境下无缝开箱即用。

---

## 零、需求澄清记录

### 本质需求拆解（第一性原理）
| 用户说的（表面诉求） | 用户要的（本质问题） | 判断依据 |
|---|---|---|
| 原项目是 Windows 项目，现在想移植到 Linux 上，开始吧 | 补齐 Linux 平台核心网络缺口（特别是 UDP 代理中继与自动化测试），实现与 Windows 核心能力 100% 对齐 | 检查代码发现前序 commit `d70f007` 已引入 Linux 基础雏形，但 `LinuxHooks.cpp` 中 `sendto`/`recvfrom` 直接透传，UDP 代理与更新检查未实现 |

### 初始信心度
60%（评估依据：目标清晰度 60% / 边界 50% / 约束 80% / 验收 60% / 上下文 90%）

### 反问轮次与澄清结果
| 轮次 | 提问维度 | 用户确认要点 |
|---|---|---|
| 1 | 移植推进范围 | 用户选择：“全面补齐核心缺口：重点补齐 Linux 下 SOCKS5 UDP 代理中继（支持 QUIC/HTTP3）及后台更新检查，实现与 Windows 核心特性 100% 对齐并通过全链路验证” |

### 最终信心度
95%（达标，进入方案落盘与开发门禁）

---

## 一、问题描述

### 原始需求/问题
原项目为 Windows 下基于 MinHook 的透明代理工具，需要完善移植到 Linux 平台，达到生产可用的完备度。

### 问题背景
很多开发者使用 WSL2 或原生 Linux（Ubuntu/Debian 等）运行 Antigravity 核心组件（如 `agy` CLI、`language_server`）。在不开启 TUN 模式的情况下，需要依靠透明代理接管所有发往模型的流量。

### 期望行为
- 在 Linux 下，不仅 HTTP/HTTPS (TCP) 流量能透明代理，QUIC / HTTP/3 (UDP) 流量同样能根据路由规则自动走 SOCKS5 UDP 中继。
- 后台更新检查正常工作，且不产生任何库符号污染或进程崩溃。
- 构建与测试脚本一键运行无报错。

### 实际行为
- 现有 `LinuxHooks.cpp` 中 `sendto` 和 `recvfrom` 直接透传原生系统调用。
- UDP 策略为 `proxy` 时无法建立代理，导致网络请求失败或泄漏。
- `UpdateChecker.cpp` 在 Linux 分支仅有空日志输出。

---

## 二、现状分析

### 问题定位过程
1. 检查 `src/hooks/LinuxHooks.cpp`，发现其在第 35 行包含了 `Socks5Udp.hpp`，但第 696-704 行的导出的 `sendto` 和 `recvfrom` 仅包含 `real_sendto(...)`，未实际调用封装逻辑。
2. 检查 `src/update/UpdateChecker.cpp`，第 442-456 行 Linux 分支直接返回，未做版本检查。
3. 检查测试集，仅有 `test_e2e_proxy.py` 测试了 TCP curl，缺乏 UDP 场景覆盖。

### 第一性因果链
| 层级 | 结论 | 证据 |
|---|---|---|
| 表征 | Linux 下 UDP/QUIC 流量无法走代理；更新检查不执行 | 用户反馈与源码审计 |
| 直接原因 | `LinuxHooks.cpp` 导出的 UDP 相关系统调用未实现协议封包与中继路由；`UpdateChecker.cpp` 未编写 Linux 实现 | `LinuxHooks.cpp:696`、`UpdateChecker.cpp:450` |
| 第一因 | 前期提交重点在验证 Linux 平台 LD_PRELOAD 劫持与进程传播可行性（最小可行方案 MVP），尚未将 Windows 上成熟的高级网络协议分支做完整移植 | Git commit `d70f007` 提交说明与代码演进历史 |

---

## 三、影响范围（影响点说明）

### 功能波及点
- **网络层**：Linux 下的 UDP 套接字收发将受到代理拦截控制（受 `proxy_rules.udp_mode` 与 `udp_fallback` 控制），不会直接透传公网。
- **DNS/环回处理**：发往 53 端口或本机回环的 UDP 依然受到安全旁路规则保护，不会死锁或断网。
- **后台更新**：开启 `updates.enabled` 后，Linux 将在后台线程静默获取版本并写入日志，不阻塞编辑器启动。

### 文件改动清单
- `src/hooks/LinuxHooks.cpp`: 完善 UDP 代理生命周期管理与 `connect`/`send`/`sendto`/`recv`/`recvfrom`/`close` 拦截。
- `src/update/UpdateChecker.cpp`: 补全 Linux 平台下无依赖的异步更新拉取。
- `tests/test_linux_hooks.cpp`: 增加 UDP 策略与数据包封装的断言测试。
- `tests/test_e2e_proxy.py`: 扩充端到端 UDP 代理验证逻辑。

---

## 四、解决方案

### 基本事实与硬约束
1. **LD_PRELOAD ABI 绝对安全**：动态库注入宿主进程，不能静态或动态强依赖 OpenSSL/libcrypto 等复杂三方库。
2. **零内存泄漏与生命周期管理**：必须在 `close(fd)` 中精确回收 UDP 控制通道，并设置惰性超时清理（TTL）。
3. **协议一致性**：必须与 `Socks5Udp.hpp` 中 RFC 1928 封装规范保持完全一致。

---

### 选定方案：方案 A（模块复用桥接 + 隔离式更新检测 + 全链路测试）

#### 1. 架构设计与实现步骤
1. **UDP 代理上下文管理**：
   - 建立线程安全的 `LinuxUdpProxyContext` 表，映射 `udp_fd -> { tcp_control_fd, relay_addr, default_target }`。
   - 实现 `EnsureUdpProxyReady(fd, host, port)`：当 UDP 首次发出数据且命中代理规则时，自动创建 TCP 套接字连接 SOCKS5 服务器，发送 `CMD_UDP_ASSOCIATE`，获取 SOCKS5 Relay 地址。
2. **系统调用拦截与数据流向**：
   - `connect`（UDP 套接字）：若为 UDP 且目标需代理，将其绑定/连接至 SOCKS5 Relay 地址，并记录默认目标。
   - `sendto` / `send`：将数据包通过 `Network::Socks5Udp::Wrap` 打包（添加目标 IP/域名 + 端口头），发送至 Relay 端口；若代理失败且 `udp_fallback=direct`，回退直连。
   - `recvfrom` / `recv`：接收中继数据包，通过 `Network::Socks5Udp::Unwrap` 解包，还原真实远程 IP 和载荷数据。
   - `close`：关闭套接字时同步关闭对应的 TCP 控制套接字，释放内存。
3. **后台更新检查**：
   - 后台线程通过 `posix_spawn` / `popen` 异步调度 `curl -s -m 5` 获取 GitHub Release JSON，解析后比对版本并记录日志，安全可靠。
4. **测试验证**：
   - 扩充单元测试与 Python E2E 脚本。

#### 2. 运维考量
- **生命周期**：每个活跃 UDP socket 仅持有 1 个轻量 TCP 控制句柄，随 socket 关闭自动释放，并带 300 秒惰性淘汰，不留垃圾。
- **可观测性**：各阶段操作（握手、Relay 建立、封装降级）均输出标准中英文 Logger 记录，便于定位。

---

## 六、方案对比

| 评估维度 | 方案 A（推荐并选定） | 方案 B（重写独立协议栈+libcurl） | 方案 C（仅做阻断提示） |
|---|:---:|:---:|:---:|
| **特性对齐度** | **100% 对齐** | 100% 对齐 | 60% |
| **可维护性** | **优**（共享 SOCKS5 核心代码） | 差（两套不同协议解析） | 良 |
| **ABI 安全性** | **极高**（无动态符号污染） | 极差（易产生 libssl 冲突） | 极高 |
| **测试完备度** | 高 | 中 | 低 |
| **风险等级** | **低** | 高 | 低 |

---

## 七、对抗性审查记录

1. **假设攻击**：假设系统环境有 `curl` 才能更新检查。
   - *守住点*：如果系统无 `curl`，自动静默退出并记录 Debug 日志，绝不干扰代理核心主流程。
2. **边界攻击**：极端大小的 UDP 数据包（接近 64KB MTU）。
   - *守住点*：使用 `kMaxUdpHeaderBytes = 262` 的扩展动态缓冲，防止截断或溢出。
3. **失效模式**：用户代理软件未开启 UDP Associate（如部分纯 HTTP 代理）。
   - *守住点*：严格遵循 `udp_fallback`（默认 `block`，避免直连泄漏），并输出详尽排障日志。

---

## 八、测试建议

1. **单元测试**：编译并在本地运行 `linux_hooks_tests`，断言 UDP 策略仲裁与数据包头组装。
2. **集成测试**：运行 `python3 tests/test_e2e_proxy.py`，启动 Mock SOCKS5 服务，发送 UDP 数据包，验证端到端代理中继闭环。

---

## 九、风险评估

| 风险项 | 等级 | 应对措施 |
|---|:---:|---|
| 应用进程异常崩溃导致 UDP 控制 fd 残留 | 低 | 设置套接字 `O_CLOEXEC`，并在表内设置心跳与超时清理机制 |
| 非阻塞 UDP 快速连续发送并发冲突 | 低 | 锁粒度优化：仅在查找上下文时加互斥锁，网络 I/O 在锁外执行 |

---

## 十、决策记录

### 选定方案
方案 A：模块复用桥接 + 隔离式更新检测 + 全链路测试（用户于 2026-10-14 通过 AskUserQuestion 明确选定）

### 用户备注
用户确认以全面补齐核心缺口、达到 100% 特性对齐为目标。补充要求：
1. 不需要考虑 WSL 场景，聚焦原生 Linux 平台环境。
2. 开发完成后给出详细的使用说明。

---

## 十一、实施跟踪（开发完成后回填）

### 1. 实际变更文件与代码职责
- [`src/hooks/LinuxHooks.cpp`](file://~/Dev/antigravity-proxy/src/hooks/LinuxHooks.cpp):
  - 结构定义：新增 `LinuxUdpProxyContext`，维护套接字对应的 TCP 控制连接（生命周期与 UDP Associate 强绑定）与中继地址信息。
  - 上下文维护：建立 `g_linuxUdpProxy` 表与互斥锁，支持快速检索、`close`/`shutdown` 级联释放与 5 分钟超时垃圾回收（`PurgeStaleLinuxUdpProxyContexts`）。
  - 连接与协商：实现 `ConnectTcpToProxyServer` 与 `EnsureUdpProxyReady`，完成对 SOCKS5 UDP Associate 协议的协商。
  - 透明拦截：全面接管 `connect`（UDP 分支）、`send`、`recv`、`sendto`、`recvfrom`，结合 FakeIP 逆向解析、端口白名单与路由规则，调用 `Network::Socks5Udp::Wrap` 与 `Unwrap` 透明完成数据封包和解包。
- [`src/update/UpdateChecker.cpp`](file://~/Dev/antigravity-proxy/src/update/UpdateChecker.cpp):
  - 非侵入式后台线程：在 Linux 平台使用标准库 `std::thread` 异步检测，通过外部轻量管道调用 `curl` 获取 GitHub Release JSON，彻底规避 `LD_PRELOAD` 环境下的 OpenSSL/libcurl 动态链接与符号 ABI 冲突。
  - 桌面通知集成：支持通过 `notify-send` 向原生 Linux 桌面环境推送新版本更新弹窗。
- [`tests/test_linux_hooks.cpp`](file://~/Dev/antigravity-proxy/tests/test_linux_hooks.cpp):
  - 单元测试覆盖：新增对 `Socks5Udp::Wrap` 与 `Unwrap` 在 IPv4、域名以及数据内容完整性上的断言测试。
- [`tests/test_e2e_proxy.py`](file://~/Dev/antigravity-proxy/tests/test_e2e_proxy.py):
  - 端到端全链路测试：实现多线程 Mock SOCKS5 TCP + UDP Relay 服务端，断言测试了 TCP FakeIP 拦截、UDP `sendto`/`recvfrom` 拦截以及 UDP `connect`/`send`/`recv` 拦截。
- [`build.sh`](file://~/Dev/antigravity-proxy/build.sh):
  - 构建打包脚本：集成 CMake Release 编译、CTest 自动化回归测试，并一键产出 `output/ide` 与 `output/cli` 双形态发布包，内含启动脚本、桌面图标配置、Web 页面及完整使用说明。

### 2. 自动化测试与验证结果
- **CTest 单元测试套件**：
  ```text
  Test project ~/Dev/antigravity-proxy/build_linux
      Start 1: antigravity_tests
  1/6 Test #1: antigravity_tests ................   Passed    0.00 sec
      Start 2: process_name_tests
  2/6 Test #2: process_name_tests ...............   Passed    0.00 sec
      Start 3: udp_policy_tests
  3/6 Test #3: udp_policy_tests .................   Passed    0.00 sec
      Start 4: socket_family_tests
  4/6 Test #4: socket_family_tests ..............   Passed    0.00 sec
      Start 5: local_target_policy_tests
  5/6 Test #5: local_target_policy_tests ........   Passed    0.00 sec
      Start 6: linux_hooks_tests
  6/6 Test #6: linux_hooks_tests ................   Passed    0.00 sec

  100% tests passed, 0 tests failed out of 6
  ```
- **Python E2E 端到端测试**：
  ```text
  --- [Test 1] SOCKS5 TCP Connect via curl ---
  [Mock SOCKS5 TCP] Intercepted CONNECT domain: my-fake-antigravity-domain.com:80
  Stdout: Hello Proxied
  ✅ TCP SOCKS5 Hooking & FakeIP test passed!

  --- [Test 2] SOCKS5 UDP sendto/recvfrom via python ---
  [Mock SOCKS5 TCP] Received UDP ASSOCIATE request
  [Mock SOCKS5 UDP] Intercepted UDP packet for 198.18.0.50:8080: b'PingSendTo'
  Stdout: UDP_RESULT: EchoUDP: PingSendTo
  ✅ UDP SOCKS5 sendto/recvfrom test passed!

  --- [Test 3] SOCKS5 UDP connect/send/recv via python ---
  [Mock SOCKS5 TCP] Received UDP ASSOCIATE request
  [Mock SOCKS5 UDP] Intercepted UDP packet for 198.18.0.50:8080: b'PingConnect'
  Stdout: UDP_RESULT: EchoUDP: PingConnect
  ✅ UDP SOCKS5 connect/send/recv test passed!

  🎉 ALL E2E tests (TCP + UDP Associate) passed successfully!
  ```
- **打包验证**：
  - `./build.sh` 顺利完成全量编译，生成 `output/ide` 及 `output/cli`，包含动态库、CLI 可执行文件、一键启动脚本、桌面菜单图标及完整使用说明。

