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

# 保留可能存在的用户已有配置 (避免重新编译覆盖用户的代理端口与规则配置)
OLD_IDE_CONFIG=""
OLD_CLI_CONFIG=""
[ -f "${IDE_OUTPUT_DIR}/config.json" ] && OLD_IDE_CONFIG="$(cat "${IDE_OUTPUT_DIR}/config.json")"
[ -f "${CLI_OUTPUT_DIR}/config.json" ] && OLD_CLI_CONFIG="$(cat "${CLI_OUTPUT_DIR}/config.json")"

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

if [ -n "${OLD_IDE_CONFIG}" ]; then
    echo "${OLD_IDE_CONFIG}" > "${IDE_OUTPUT_DIR}/config.json"
else
    echo "${CONFIG_JSON_CONTENT}" > "${IDE_OUTPUT_DIR}/config.json"
fi

if [ -n "${OLD_CLI_CONFIG}" ]; then
    echo "${OLD_CLI_CONFIG}" > "${CLI_OUTPUT_DIR}/config.json"
else
    echo "${CONFIG_JSON_CONTENT}" > "${CLI_OUTPUT_DIR}/config.json"
fi

# ============================================================
# 生成 IDE 专属产出脚本与说明
# ============================================================
cat << 'EOF' > "${IDE_OUTPUT_DIR}/antigravity-ide.sh"
#!/usr/bin/env bash
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export LD_PRELOAD="${DIR}/libantigravity_proxy.so:${LD_PRELOAD}"
export ANTIGRAVITY_PROXY_CONFIG="${DIR}/config.json"

# 解析 config.json 中的代理配置
PROXY_ARGS=()
if [ -f "${DIR}/config.json" ]; then
    PARSE_OUT=$(python3 -c '
import json, sys
try:
    with open(sys.argv[1]) as f:
        cfg = json.load(f)
    p = cfg.get("proxy", {})
    port = p.get("port", 0)
    if port:
        host = p.get("host", "127.0.0.1")
        ptype = p.get("type", "socks5") or "socks5"
        print(f"{ptype} {host} {port}")
except Exception:
    pass
' "${DIR}/config.json" 2>/dev/null)
    if [ -n "${PARSE_OUT}" ]; then
        read -r P_TYPE P_HOST P_PORT <<< "${PARSE_OUT}"
        PROXY_URL="${P_TYPE}://${P_HOST}:${P_PORT}"
        PROXY_ARGS+=(
            "--proxy-server=${PROXY_URL}"
            "--proxy-bypass-list=localhost;127.0.0.1;::1;<-loopback>"
        )
    fi
fi

# 查找 Antigravity IDE 可执行文件
IDE_BIN="${ANTIGRAVITY_IDE_PATH}"
if [ -z "${IDE_BIN}" ]; then
    for candidate in "${HOME}/Applications/Antigravity IDE/antigravity-ide" "${HOME}/.local/bin/antigravity-ide" /opt/Antigravity-IDE/antigravity-ide /opt/antigravity-ide/antigravity-ide /usr/local/bin/antigravity-ide /usr/bin/antigravity-ide antigravity-ide; do
        if [ -f "${candidate}" ] || command -v "${candidate}" >/dev/null 2>&1; then
            IDE_BIN="${candidate}"
            break
        fi
    done
fi

if [ -z "${IDE_BIN}" ]; then
    echo "❌ 未检测到 Antigravity IDE 可执行程序。"
    echo "请通过环境变量指定路径启动，例如:"
    echo "  ANTIGRAVITY_IDE_PATH=\"/path/to/antigravity-ide\" $0"
    exit 1
fi

echo "🚀 正在启动 Antigravity IDE (已注入立体代理 ${PROXY_URL:-已加载}): ${IDE_BIN}"
exec "${IDE_BIN}" "${PROXY_ARGS[@]}" "$@"
EOF
chmod +x "${IDE_OUTPUT_DIR}/antigravity-ide.sh"

cat << 'EOF' > "${IDE_OUTPUT_DIR}/antigravity.sh"
#!/usr/bin/env bash
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export LD_PRELOAD="${DIR}/libantigravity_proxy.so:${LD_PRELOAD}"
export ANTIGRAVITY_PROXY_CONFIG="${DIR}/config.json"

# 解析 config.json 中的代理配置
PROXY_ARGS=()
if [ -f "${DIR}/config.json" ]; then
    PARSE_OUT=$(python3 -c '
import json, sys
try:
    with open(sys.argv[1]) as f:
        cfg = json.load(f)
    p = cfg.get("proxy", {})
    port = p.get("port", 0)
    if port:
        host = p.get("host", "127.0.0.1")
        ptype = p.get("type", "socks5") or "socks5"
        print(f"{ptype} {host} {port}")
except Exception:
    pass
' "${DIR}/config.json" 2>/dev/null)
    if [ -n "${PARSE_OUT}" ]; then
        read -r P_TYPE P_HOST P_PORT <<< "${PARSE_OUT}"
        PROXY_URL="${P_TYPE}://${P_HOST}:${P_PORT}"
        PROXY_ARGS+=(
            "--proxy-server=${PROXY_URL}"
            "--proxy-bypass-list=localhost;127.0.0.1;::1;<-loopback>"
        )
    fi
fi

# 查找 Antigravity 智能体桌面端可执行文件
APP_BIN="${ANTIGRAVITY_PATH}"
if [ -z "${APP_BIN}" ]; then
    for candidate in "${HOME}/Applications/Antigravity-x64/antigravity" "${HOME}/Applications/antigravity/antigravity" "${HOME}/.local/share/antigravity/antigravity" "${HOME}/.local/bin/antigravity" /opt/Antigravity/antigravity /opt/antigravity/antigravity /usr/local/bin/antigravity /usr/bin/antigravity antigravity; do
        if [ -f "${candidate}" ] || command -v "${candidate}" >/dev/null 2>&1; then
            APP_BIN="${candidate}"
            break
        fi
    done
fi

if [ -z "${APP_BIN}" ]; then
    echo "❌ 未检测到 Antigravity 桌面客户端可执行程序。"
    echo "请通过环境变量指定路径启动，例如:"
    echo "  ANTIGRAVITY_PATH=\"/path/to/antigravity\" $0"
    exit 1
fi

echo "🚀 正在启动 Antigravity 桌面客户端 (已注入立体代理 ${PROXY_URL:-已加载}): ${APP_BIN}"
exec "${APP_BIN}" "${PROXY_ARGS[@]}" "$@"
EOF
chmod +x "${IDE_OUTPUT_DIR}/antigravity.sh"

cat << 'EOF' > "${IDE_OUTPUT_DIR}/patch-desktop.sh"
#!/usr/bin/env bash
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DESKTOP_DIR="${HOME}/.local/share/applications"
mkdir -p "${DESKTOP_DIR}"

PATCHED_COUNT=0

# 辅助函数：从已有 desktop 文件读取 Icon 属性
get_icon_from_desktop() {
    local dfile="$1"
    if [ -f "${dfile}" ]; then
        local icon_line
        icon_line=$(grep -m1 '^Icon=' "${dfile}" 2>/dev/null | cut -d'=' -f2-)
        if [ -n "${icon_line}" ]; then
            echo "${icon_line}"
            return 0
        fi
    fi
    return 1
}

# 1. 检测并生成 Antigravity IDE (Proxied) 桌面项
IDE_BIN="${ANTIGRAVITY_IDE_PATH}"
if [ -z "${IDE_BIN}" ]; then
    for candidate in "${HOME}/Applications/Antigravity IDE/antigravity-ide" "${HOME}/.local/bin/antigravity-ide" /opt/Antigravity-IDE/antigravity-ide /opt/antigravity-ide/antigravity-ide /usr/local/bin/antigravity-ide /usr/bin/antigravity-ide antigravity-ide; do
        if [ -f "${candidate}" ] || command -v "${candidate}" >/dev/null 2>&1; then
            IDE_BIN="${candidate}"
            break
        fi
    done
fi

if [ -n "${IDE_BIN}" ] || [ -f "${DIR}/antigravity-ide.sh" ]; then
    IDE_TARGET="${DESKTOP_DIR}/antigravity-ide-proxied.desktop"
    
    # 查找最契合的图标
    IDE_ICON=$(get_icon_from_desktop "${DESKTOP_DIR}/antigravity-ide.desktop")
    if [ -z "${IDE_ICON}" ]; then
        for ic in "${HOME}/Applications/Antigravity IDE/antigravity-ide.png" "${HOME}/Applications/Antigravity IDE/resources/app/resources/linux/code.png" "${HOME}/.icons/antigravity-ide.png" "${HOME}/Applications/.icons/antigravity-ide.png"; do
            if [ -f "${ic}" ]; then
                IDE_ICON="${ic}"
                break
            fi
        done
    fi
    [ -z "${IDE_ICON}" ] && IDE_ICON="antigravity-ide"

    cat << DESKTOP_IDE_EOF > "${IDE_TARGET}"
[Desktop Entry]
Version=1.0
Type=Application
Name=Antigravity IDE (Proxied)
Name[zh_CN]=Antigravity IDE (代理版)
Comment=Antigravity IDE with Transparent Proxy
Comment[zh_CN]=Antigravity IDE 开发环境 (带透明代理)
Exec=${DIR}/antigravity-ide.sh %F
Icon=${IDE_ICON}
StartupNotify=true
StartupWMClass=antigravity-ide
Categories=Development;IDE;TextEditor;
DESKTOP_IDE_EOF
    chmod +x "${IDE_TARGET}" 2>/dev/null || true
    echo "✅ 已生成 Antigravity IDE 代理桌面项: ${IDE_TARGET}"
    PATCHED_COUNT=$((PATCHED_COUNT + 1))
else
    echo "ℹ️ 未检测到 Antigravity IDE，跳过生成 antigravity-ide-proxied.desktop"
fi

# 2. 检测并生成 Antigravity 智能体桌面端 (Proxied) 桌面项
APP_BIN="${ANTIGRAVITY_PATH}"
if [ -z "${APP_BIN}" ]; then
    for candidate in "${HOME}/Applications/Antigravity-x64/antigravity" "${HOME}/Applications/antigravity/antigravity" "${HOME}/.local/share/antigravity/antigravity" "${HOME}/.local/bin/antigravity" /opt/Antigravity/antigravity /opt/antigravity/antigravity /usr/local/bin/antigravity /usr/bin/antigravity antigravity; do
        if [ -f "${candidate}" ] || command -v "${candidate}" >/dev/null 2>&1; then
            APP_BIN="${candidate}"
            break
        fi
    done
fi

if [ -n "${APP_BIN}" ] || [ -f "${DIR}/antigravity.sh" ]; then
    APP_TARGET="${DESKTOP_DIR}/antigravity-proxied.desktop"

    # 查找最契合的图标
    APP_ICON=$(get_icon_from_desktop "${DESKTOP_DIR}/antigravity.desktop")
    if [ -z "${APP_ICON}" ]; then
        for ic in "${HOME}/Applications/Antigravity-x64/antigravity.png" "${HOME}/Applications/.icons/antigravity.png" "${HOME}/.icons/antigravity.png"; do
            if [ -f "${ic}" ]; then
                APP_ICON="${ic}"
                break
            fi
        done
    fi
    [ -z "${APP_ICON}" ] && APP_ICON="antigravity"

    cat << DESKTOP_APP_EOF > "${APP_TARGET}"
[Desktop Entry]
Version=1.0
Type=Application
Name=Antigravity (Proxied)
Name[zh_CN]=Antigravity (代理版)
Comment=Antigravity Agentic Desktop Application with Transparent Proxy
Comment[zh_CN]=Antigravity 智能体桌面应用 (带透明代理)
Exec=${DIR}/antigravity.sh %U
Icon=${APP_ICON}
StartupNotify=true
StartupWMClass=antigravity
Categories=Development;Utility;
DESKTOP_APP_EOF
    chmod +x "${APP_TARGET}" 2>/dev/null || true
    echo "✅ 已生成 Antigravity 智能体桌面端代理桌面项: ${APP_TARGET}"
    PATCHED_COUNT=$((PATCHED_COUNT + 1))
else
    echo "ℹ️ 未检测到 Antigravity 桌面端，跳过生成 antigravity-proxied.desktop"
fi

if [ "${PATCHED_COUNT}" -eq 0 ]; then
    echo "⚠️ 未检测到任何 Antigravity 应用。若安装在非标准目录，可通过环境变量指定后重试，例如:"
    echo "  ANTIGRAVITY_IDE_PATH=\"/path/to/antigravity-ide\" ANTIGRAVITY_PATH=\"/path/to/antigravity\" $0"
    exit 1
fi

echo "🎉 桌面快捷启动项配置完成！您现在可在桌面启动器中直接点击带有 (Proxied/代理版) 标识的图标。"
EOF
chmod +x "${IDE_OUTPUT_DIR}/patch-desktop.sh"

cat << 'EOF' > "${IDE_OUTPUT_DIR}/使用说明.md"
# Antigravity-Proxy Linux IDE 产出使用说明

## 目录文件说明
- `libantigravity_proxy.so`：Linux 核心网络重定向动态库（基于 LD_PRELOAD 实现透明劫持）
- `config.json`：代理与 FakeIP 配置文件
- `antigravity-ide.sh`：一键启动 **Antigravity IDE**（代码编辑器/开发环境）脚本
- `antigravity.sh`：一键启动 **Antigravity**（智能体桌面端客户端）脚本
- `patch-desktop.sh`：一键为 Linux 桌面系统智能生成双应用桌面启动图标
- `config-web.html`：图形化配置页面，可在浏览器中双击直观修改代理端口与模式
- `antigravity-proxy`：全功能命令行管理与状态测试工具

---

## 快速使用

### 1. 启动 Antigravity IDE (开发环境)
在当前目录下运行：
```bash
./antigravity-ide.sh
```

### 2. 启动 Antigravity 智能体桌面端
在当前目录下运行：
```bash
./antigravity.sh
```

### 3. 一键生成桌面启动菜单图标 (推荐)
运行：
```bash
./patch-desktop.sh
```
该脚本会自动检测系统中安装的 Antigravity IDE 与 Antigravity 桌面客户端，并分别在系统应用菜单（GNOME / KDE / XFCE 等）生成：
- **Antigravity IDE (Proxied)**：代码开发环境
- **Antigravity (Proxied)**：智能体桌面客户端

点击即可直接在代理环境下运行！

---

## 非标准安装路径配置说明

若您的 Antigravity 安装在自定义或非标准目录（不在系统 PATH 或标准 Applications 目录中），可通过以下方式指定路径：

### 方式一：临时环境变量启动
```bash
# 启动 IDE
ANTIGRAVITY_IDE_PATH="/opt/custom-path/antigravity-ide" ./antigravity-ide.sh

# 启动智能体桌面端
ANTIGRAVITY_PATH="/opt/custom-path/antigravity" ./antigravity.sh
```

### 方式二：生成指定路径的桌面菜单图标
```bash
ANTIGRAVITY_IDE_PATH="/opt/custom-path/antigravity-ide" \
ANTIGRAVITY_PATH="/opt/custom-path/antigravity" \
./patch-desktop.sh
```

### 方式三：写入用户环境配置 (永久生效)
在 `~/.bashrc` 或 `~/.zshrc` 末尾添加：
```bash
export ANTIGRAVITY_IDE_PATH="/path/to/your/antigravity-ide"
export ANTIGRAVITY_PATH="/path/to/your/antigravity"
```
保存后执行 `source ~/.bashrc` 即可。

---

## 状态检测与连通性测试
使用随附的命令行工具快速排查：
```bash
# 查看当前代理与程序检测状态
./antigravity-proxy status

# 测试配置的代理端口连通性
./antigravity-proxy test
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

if [ -f "${DIR}/config.json" ]; then
    PARSE_OUT=$(python3 -c '
import json, sys
try:
    with open(sys.argv[1]) as f:
        cfg = json.load(f)
    p = cfg.get("proxy", {})
    port = p.get("port", 0)
    if port:
        host = p.get("host", "127.0.0.1")
        ptype = p.get("type", "socks5") or "socks5"
        print(f"{ptype} {host} {port}")
except Exception:
    pass
' "${DIR}/config.json" 2>/dev/null)
    if [ -n "${PARSE_OUT}" ]; then
        read -r P_TYPE P_HOST P_PORT <<< "${PARSE_OUT}"
        PROXY_URL="${P_TYPE}://${P_HOST}:${P_PORT}"
        export ALL_PROXY="${PROXY_URL}"
        export all_proxy="${PROXY_URL}"
        export HTTP_PROXY="${PROXY_URL}"
        export http_proxy="${PROXY_URL}"
        export HTTPS_PROXY="${PROXY_URL}"
        export https_proxy="${PROXY_URL}"
        export NO_PROXY="localhost,127.0.0.1,::1"
        export no_proxy="localhost,127.0.0.1,::1"
    fi
fi

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
echo "  - antigravity.sh         (一键启动智能体桌面端脚本)"
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
