#!/usr/bin/env bash
set -e

# ==============================================================================
# Antigravity-Proxy Linux Build & Package Script (IDE & CLI)
# ==============================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build_linux"
OUTPUT_DIR="${SCRIPT_DIR}/output"
IDE_OUTPUT_DIR="${OUTPUT_DIR}/ide"
CLI_OUTPUT_DIR="${OUTPUT_DIR}/cli"
ARCH="$(uname -m)"

echo "=================================================="
echo "🚀 编译 Antigravity-Proxy (Linux ${ARCH})"
echo "=================================================="

# 检查编译器和工具
command -v cmake >/dev/null 2>&1 || { echo "❌ 缺少 cmake，请先安装: apt install cmake"; exit 1; }
command -v g++ >/dev/null 2>&1 || command -v clang++ >/dev/null 2>&1 || { echo "❌ 缺少 C++ 编译器，请先安装: apt install build-essential"; exit 1; }

# 选择生成器
GENERATOR="Unix Makefiles"
if command -v ninja >/dev/null 2>&1; then
    GENERATOR="Ninja"
fi

echo "📦 配置 CMake (生成器: ${GENERATOR})..."
cmake -B "${BUILD_DIR}" -G "${GENERATOR}" -DCMAKE_BUILD_TYPE=Release "${SCRIPT_DIR}"

echo "🔨 开始编译..."
cmake --build "${BUILD_DIR}" --config Release

echo "🧪 运行自动化测试..."
ctest --test-dir "${BUILD_DIR}" --output-on-failure

# 准备发布目录结构 (与 Windows build.ps1 保持一致: output/ide 与 output/cli)
echo "📁 准备发布文件..."
rm -rf "${IDE_OUTPUT_DIR}" "${CLI_OUTPUT_DIR}"
mkdir -p "${IDE_OUTPUT_DIR}" "${CLI_OUTPUT_DIR}"

# 核心库与工具复制
cp "${BUILD_DIR}/libantigravity_proxy.so" "${IDE_OUTPUT_DIR}/"
cp "${BUILD_DIR}/libantigravity_proxy.so" "${CLI_OUTPUT_DIR}/"
cp "${BUILD_DIR}/antigravity-proxy" "${CLI_OUTPUT_DIR}/"
cp "${BUILD_DIR}/antigravity-proxy" "${IDE_OUTPUT_DIR}/"

# 复制 Web 配置界面
if [ -f "${SCRIPT_DIR}/resources/config-web/index.html" ]; then
    cp "${SCRIPT_DIR}/resources/config-web/index.html" "${IDE_OUTPUT_DIR}/config-web.html"
    cp "${SCRIPT_DIR}/resources/config-web/index.html" "${CLI_OUTPUT_DIR}/config-web.html"
fi

# 生成默认 config.json 内容
CONFIG_JSON_CONTENT='{
  "_version": "1.0.0",
  "log_level": "info",
  "proxy": {
    "host": "127.0.0.1",
    "port": 7890,
    "type": "socks5"
  },
  "fake_ip": {
    "enabled": true,
    "cidr": "198.18.0.0/15"
  },
  "timeout": {
    "connect": 5000,
    "send": 5000,
    "recv": 5000
  },
  "updates": {
    "enabled": false
  },
  "traffic_logging": false,
  "child_injection": true,
  "child_injection_mode": "filtered",
  "child_injection_exclude": [],
  "target_processes": [
    "antigravity",
    "antigravity.exe",
    "Antigravity IDE.exe",
    "language_server",
    "language_server.exe",
    "language_server_linux",
    "agy",
    "agy.exe",
    "node"
  ],
  "proxy_rules": {
    "allowed_ports": [80, 443],
    "dns_mode": "direct",
    "ipv6_mode": "proxy",
    "udp_mode": "auto",
    "udp_fallback": "block",
    "routing": {
      "enabled": true,
      "priority_mode": "order",
      "default_action": "proxy",
      "use_default_private": true,
      "rules": []
    }
  }
}'

echo "${CONFIG_JSON_CONTENT}" > "${IDE_OUTPUT_DIR}/config.json"
echo "${CONFIG_JSON_CONTENT}" > "${CLI_OUTPUT_DIR}/config.json"

# ============================================================
# 生成 IDE 专属产出脚本与说明
# ============================================================
cat << 'EOF' > "${IDE_OUTPUT_DIR}/antigravity-ide.sh"
#!/usr/bin/env bash
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export LD_PRELOAD="${DIR}/libantigravity_proxy.so:${LD_PRELOAD}"
export ANTIGRAVITY_PROXY_CONFIG="${DIR}/config.json"

# 查找 Antigravity IDE 可执行文件
IDE_BIN="${ANTIGRAVITY_IDE_PATH}"
if [ -z "${IDE_BIN}" ]; then
    for candidate in antigravity /usr/bin/antigravity /usr/local/bin/antigravity /opt/Antigravity/antigravity /opt/antigravity/antigravity "${HOME}/.local/share/antigravity/antigravity"; do
        if command -v "${candidate}" >/dev/null 2>&1 || [ -f "${candidate}" ]; then
            IDE_BIN="${candidate}"
            break
        fi
    done
fi

if [ -z "${IDE_BIN}" ]; then
    echo "❌ 未检测到 Antigravity IDE 可执行程序。"
    echo "请通过环境变量指定路径启动，例如: ANTIGRAVITY_IDE_PATH=/path/to/antigravity $0"
    exit 1
fi

echo "🚀 正在启动 Antigravity IDE (已注入代理): ${IDE_BIN}"
exec "${IDE_BIN}" "$@"
EOF
chmod +x "${IDE_OUTPUT_DIR}/antigravity-ide.sh"

cat << 'EOF' > "${IDE_OUTPUT_DIR}/patch-desktop.sh"
#!/usr/bin/env bash
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DESKTOP_DIR="${HOME}/.local/share/applications"
mkdir -p "${DESKTOP_DIR}"
TARGET="${DESKTOP_DIR}/antigravity-proxied.desktop"

cat << DESKTOP_EOF > "${TARGET}"
[Desktop Entry]
Name=Antigravity (Proxied)
Comment=Antigravity Editor with Transparent Proxy
GenericName=Text Editor
Exec=${DIR}/antigravity-ide.sh %F
Icon=antigravity
Type=Application
StartupNotify=true
StartupWMClass=Antigravity
Categories=Development;TextEditor;IDE;
DESKTOP_EOF

echo "✅ 已生成桌面启动项: ${TARGET}"
echo "您现在可以在 Linux 应用菜单或启动器中直接点击 'Antigravity (Proxied)' 启动。"
EOF
chmod +x "${IDE_OUTPUT_DIR}/patch-desktop.sh"

cat << 'EOF' > "${IDE_OUTPUT_DIR}/使用说明.md"
# Antigravity-Proxy Linux IDE 产出使用说明

## 目录文件说明
- `libantigravity_proxy.so`：Linux 核心网络重定向动态库（基于 LD_PRELOAD）
- `config.json`：代理与 FakeIP 配置文件
- `antigravity-ide.sh`：一键启动 Antigravity IDE 脚本（自动注入代理）
- `patch-desktop.sh`：一键为 Linux 桌面系统生成“带代理的 Antigravity”图标
- `config-web.html`：双击可在浏览器中直观修改代理配置
- `antigravity-proxy`：全功能管理与状态测试工具

## 使用方法

### 方法 1：脚本一键启动
在当前目录下运行：
```bash
./antigravity-ide.sh
```
若 IDE 安装在非标准目录，可通过环境变量指定：
```bash
ANTIGRAVITY_IDE_PATH=/opt/Antigravity/antigravity ./antigravity-ide.sh
```

### 方法 2：生成桌面应用菜单图标
运行：
```bash
./patch-desktop.sh
```
即可在桌面应用列表（GNOME / KDE / XFCE 等）看到并点击 `Antigravity (Proxied)` 启动。

### 方法 3：通过管理工具启动
```bash
./antigravity-proxy ide
```
EOF

# ============================================================
# 生成 CLI 专属产出脚本与说明
# ============================================================
cat << 'EOF' > "${CLI_OUTPUT_DIR}/agy.sh"
#!/usr/bin/env bash
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export LD_PRELOAD="${DIR}/libantigravity_proxy.so:${LD_PRELOAD}"
export ANTIGRAVITY_PROXY_CONFIG="${DIR}/config.json"
exec agy "$@"
EOF
chmod +x "${CLI_OUTPUT_DIR}/agy.sh"

cat << 'EOF' > "${CLI_OUTPUT_DIR}/使用说明.md"
# Antigravity-Proxy Linux CLI 产出使用说明

## 目录文件说明
- `libantigravity_proxy.so`：Linux 核心网络重定向动态库
- `antigravity-proxy`：管理与运行工具
- `agy.sh`：一键代理运行 agy 脚本
- `config.json`：代理配置文件
- `config-web.html`：图形化配置网页

## 使用方法
```bash
# 启动 agy
./antigravity-proxy agy

# 包装运行任意命令
./antigravity-proxy run curl -I https://www.google.com

# 测试代理服务器连通性
./antigravity-proxy test
```
EOF

echo ""
echo "=================================================="
echo "🎉 编译与打包成功！"
echo ""
echo "📦 IDE 产出目录: ${IDE_OUTPUT_DIR}"
echo "  - libantigravity_proxy.so (核心拦截动态库)"
echo "  - antigravity-ide.sh     (一键启动 IDE 脚本)"
echo "  - patch-desktop.sh       (生成桌面启动图标脚本)"
echo "  - config.json            (代理配置文件)"
echo "  - config-web.html        (可视化配置页面)"
echo "  - 使用说明.md            (IDE 使用指南)"
echo ""
echo "📦 CLI 产出目录: ${CLI_OUTPUT_DIR}"
echo "  - libantigravity_proxy.so (核心拦截动态库)"
echo "  - antigravity-proxy      (命令行管理工具)"
echo "  - agy.sh                 (启动 agy 脚本)"
echo "  - config.json            (代理配置文件)"
echo "  - config-web.html        (可视化配置页面)"
echo "  - 使用说明.md            (CLI 使用指南)"
echo "=================================================="

# 如果传入 --install，安装到用户目录或系统目录
if [ "$1" = "--install" ]; then
    INSTALL_BIN="/usr/local/bin"
    INSTALL_LIB="/usr/local/lib"
    if [ ! -w "/usr/local/bin" ]; then
        INSTALL_BIN="${HOME}/.local/bin"
        INSTALL_LIB="${HOME}/.local/lib"
    fi
    mkdir -p "${INSTALL_BIN}" "${INSTALL_LIB}"
    cp "${BUILD_DIR}/libantigravity_proxy.so" "${INSTALL_LIB}/"
    cp "${BUILD_DIR}/antigravity-proxy" "${INSTALL_BIN}/"
    echo "✅ 已安装到: ${INSTALL_BIN}/antigravity-proxy 及 ${INSTALL_LIB}/libantigravity_proxy.so"
fi
