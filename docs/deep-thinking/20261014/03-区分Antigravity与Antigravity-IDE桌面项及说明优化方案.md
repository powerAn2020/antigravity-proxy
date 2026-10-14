# 区分Antigravity与Antigravity-IDE桌面项及说明优化方案

```yaml
---
task:   区分Antigravity与Antigravity-IDE桌面项及说明优化方案
date:   2026-10-14
type:   优化
level:  完整
status: 待实施
chosen: 方案A（双脚本专用分发 + patch-desktop 双目标智能探测）
---
```

## 方案逻辑通俗说明（开篇必读，落盘必填）

### 问题是什么
用户在实际使用测试中发现，`Antigravity`（智能体桌面端应用）和 `Antigravity IDE`（AI 集成开发环境）均能在 Linux 下正常通过代理运行。但是现有的 `patch-desktop` 脚本和启动脚本未对两者进行明确区分，硬编码生成了一个叫 `Antigravity (Proxied)` 的快捷方式，导致两者冲突覆盖、窗口类（`StartupWMClass`）不匹配、桌面图标错乱；同时缺少对非标准安装路径时通过环境变量指定可执行文件路径的详细文档指引。

### 方案怎么解决它
本方案采用**“双脚本专用分发 + patch-desktop 双目标独立探测”**架构彻底解决区分问题：
1. **独立启动脚本分工**：
   - `antigravity-ide.sh`：专注定位并拉起 `Antigravity IDE`（如 `antigravity-ide` 二进制），支持环境变量 `ANTIGRAVITY_IDE_PATH` 自定义非标准路径；
   - `antigravity.sh`：专注定位并拉起 `Antigravity` 桌面应用（如 `antigravity` 二进制），支持环境变量 `ANTIGRAVITY_PATH` 自定义非标准路径；
   - 两者共享相同的网络代理参数与底层 `LD_PRELOAD` 劫持机制。
2. **桌面快捷方式智能双探测生成**：
   - `patch-desktop.sh` 脚本和 CLI `patch-desktop` 命令同时对 `Antigravity IDE` 和 `Antigravity` 进行独立扫描探测：
     - 若检测到 `Antigravity IDE`：生成 `antigravity-ide-proxied.desktop`，名称显示为 `Antigravity IDE (Proxied)`，WMClass 设为 `antigravity-ide`，图标指向对应 IDE 图标；
     - 若检测到 `Antigravity`：生成 `antigravity-proxied.desktop`，名称显示为 `Antigravity (Proxied)`，WMClass 设为 `antigravity`，图标指向对应桌面端图标；
     - 系统中安装了哪个就生成哪个，两者皆有时同时生成两个独立图标，在启动器中一目了然、互不干扰。
3. **完善使用说明与发版说明**：
   - 在 `使用说明.md`、`RELEASE_NOTES.md` 中增加显眼的“非标准目录路径指定”章节，明确给出 `ANTIGRAVITY_IDE_PATH=/your/path ./antigravity-ide.sh` 和 `ANTIGRAVITY_PATH=/your/path ./antigravity.sh` 的具体用法；
   - 延后发版，待本优化通过实机验证后，重新生成发版说明供用户再次审核。

### 影响点说明
- **构建脚本与模板**：`build.sh` 中更新 `patch-desktop.sh`、`antigravity-ide.sh` 并新增 `antigravity.sh` 生成逻辑；
- **CLI 管理工具**：`src/cli/main.cpp` 中将 `PatchDesktopEntry` 升级为双目标探测生成，新增 `antigravity-proxy app`（或 `antigravity`）子命令；
- **文档文件**：
  - `output/ide/使用说明.md`、`output/cli/使用说明.md` 补充环境变量说明与双产品定位；
  - `RELEASE_NOTES.md` 同步更新说明与最新产物哈希。

---

## 零、需求澄清记录

### 本质需求拆解（第一性原理）
| 用户说的（表面诉求） | 用户要的（本质问题） | 判断依据 |
|----------------------|----------------------|----------|
| patch-desktop 脚本没有区分 Antigravity 和 Antigravity IDE；使用说明补充非标准目录环境变量说明；延后发版 | 解决 Linux 桌面环境中两个异构产品（客户端与IDE）并存时的快捷方式冲突、图标与窗口类混淆问题，并提供清晰的非标准路径适配指南 | 实机存在 `~/Applications/Antigravity-x64/antigravity`（客户端）与 `~/Applications/Antigravity IDE/antigravity-ide`（IDE），两者 `StartupWMClass`、`Icon`、`Exec` 均不同 |

### 初始信心度
100%（评估依据：目标清晰度 100% / 边界 100% / 约束 100% / 验收 100% / 上下文 100%）。

### 反问轮次与澄清结果
- 无需反问，初评即 ≥95%。

### 最终信心度
100%（达标 ≥95%）。

---

## 一、问题描述

### 原始需求/问题
> “使用说明补充 ：若 IDE 安装在非标准目录，可通过环境变量的说明。经过我的测试Antigravity和Antigravity IDE都能用,但是 patch-desktop脚本没有区分Antigravity和Antigravity IDE，优化下代码，延后发版”

### 问题背景
用户在 Linux 本地环境同时部署了 `Antigravity-x64/antigravity` 和 `Antigravity IDE/antigravity-ide`。当前 `patch-desktop.sh` 只能匹配首个发现的二进制并统一命名为 `Antigravity (Proxied)`，生成单个 `.desktop` 文件，导致后安装或另一款软件无法生成独立菜单入口，任务栏窗口类错配。

---

## 二、根因分析

### 第一性因果链（三层）
| 层级 | 结论 | 证据 |
|------|------|------|
| **表征** | 桌面应用菜单中只有一个 `Antigravity (Proxied)` 图标，无法同时区分并启动 `Antigravity` 与 `Antigravity IDE` | `patch-desktop.sh` 仅生成单个 `antigravity-proxied.desktop` |
| **直接原因** | 1. 查找算法采用单变量 `IDE_BIN` 竞争赋值，谁排在前面就生成谁；<br>2. 快捷方式元数据（Name/Icon/StartupWMClass）硬编码，未按实际检测到的客户端类型分化 | `build.sh` 第 122 行与 148 行逻辑 |
| **第一因** | 原项目在 Windows 上以单个 exe 启动器为主要目标，未考虑 Linux 下同一个生态内可能同时存在“桌面智能体应用（antigravity）”与“集成开发环境（antigravity-ide）”两个并存实体的桌面规范（XDG Desktop Entry Spec）兼容性 | Linux 桌面规范中 `StartupWMClass` 必须与 X11/Wayland 窗口实例精确匹配 |

---

## 三、方案设计与对比

### 候选方案
1. **方案 A（推荐）：双脚本专用分发 + patch-desktop 双目标智能探测**
   - 彻底解耦 `Antigravity IDE`（IDE）与 `Antigravity`（桌面端）。
   - 独立提供 `antigravity-ide.sh` 与 `antigravity.sh`。
   - `patch-desktop` 分别独立探测，按需生成 `antigravity-ide-proxied.desktop` 和 `antigravity-proxied.desktop`，WMClass 与图标完全精准对应。
2. **方案 B：单脚本参数分流**
   - 通过单脚本带参数区分，但对普通桌面与终端用户极不直观，容易参数遗漏。
3. **方案 C：仅在 patch-desktop 脚本中增加分支判断**
   - 仅修复桌面快捷方式，终端启动脚本依然混淆。

---

## 四、对抗性审查（红队攻击记录）

1. **单软件安装场景**：
   - 质疑：如果机器上只安装了其中一个，会不会生成坏死的死链快捷方式？
   - 答辩：设计采用独立探测哨兵变量，只在实际探测到对应二进制时才生成对应 `.desktop` 文件，若未探测到则静默跳过或提示说明，绝不生成死链。
2. **图标路径有效性**：
   - 质疑：用户机器的图标位置可能与标准路径不同，找不到绝对路径图标怎么办？
   - 答辩：优先按优先级检测绝对路径图标文件（`~/.icons/`、`resources/app/...`），如不存在则自动降级使用系统图标主题名（`antigravity-ide`、`antigravity`），确保任何桌面环境均能正常显示。
3. **环境变量覆盖优先级**：
   - 质疑：`ANTIGRAVITY_IDE_PATH` 与 `ANTIGRAVITY_PATH` 是否会冲突？
   - 答辩：`antigravity-ide.sh` 专属识别 `ANTIGRAVITY_IDE_PATH`；`antigravity.sh` 专属识别 `ANTIGRAVITY_PATH`，职责单一，互不干扰。

---

## 决策记录

- **选定方案**：方案 A（双脚本专用分发 + patch-desktop 双目标智能探测）
- **确认时间**：2026-10-14 16:32:14
- **用户确认要点**：通过多方案点选确认采用方案 A，延后发版，优先完善区分逻辑与说明文档。
- **待验证项去向**：
  1. 验证 `patch-desktop.sh` 在当前机器上能同时成功生成两个独立的快捷方式；
  2. 验证 `antigravity.sh` 与 `antigravity-ide.sh` 分别能启动对应的目标程序；
  3. 验证文档内容准确无误后重新提交发版审核。

---

## 实施跟踪

- **实际改动文件清单**：
  1. `build.sh`：新增 `antigravity.sh` 生成；完善 `antigravity-ide.sh`；重构 `patch-desktop.sh` 支持双目标独立扫描生成；完善 `使用说明.md`。
  2. `src/cli/main.cpp`：拆分 `FindIdePath()` 与 `FindAppPath()`；新增 `app` / `antigravity` 子命令；重构 `PatchDesktopEntry()` 支持双快捷方式生成及原有图标提取；更新 `status` 与 `help`。
  3. `tests/test_e2e_proxy.py`：测试环境隔离外部代理环境变量。
  4. `RELEASE_NOTES.md`：详细记录双目标区分、非标准安装路径配置指南，以及最新资产哈希。
- **测试验证结论**：
  1. CTest 单元测试套件 6/6 全部通过。
  2. Python 端到端回归测试 4/4 全部通过（TCP 代理拦截、UDP Associate、UDP connect、Go 环境变量注入）。
  3. `patch-desktop.sh` 与 `antigravity-proxy patch-desktop` 成功在 `~/.local/share/applications/` 生成独立的 `antigravity-ide-proxied.desktop` 与 `antigravity-proxied.desktop`。
- **最终状态**：实施完成，等待用户终审发版。
