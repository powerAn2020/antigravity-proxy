#include "../platform/PlatformCompat.hpp"

#include <iostream>
#include <vector>
#include <string>
#include <cstdlib>
#include <unistd.h>
#include <limits.h>
#include <sys/stat.h>
#include <fstream>
#include <sstream>

#include "../core/Config.hpp"
#include "../core/Logger.hpp"

namespace {

std::string GetExecutableDir() {
    char buf[PATH_MAX] = {0};
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len > 0) {
        buf[len] = '\0';
        std::string s(buf);
        size_t last = s.find_last_of('/');
        if (last != std::string::npos) {
            return s.substr(0, last);
        }
    }
    return ".";
}

bool FileExists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

std::string FindProxySoPath() {
    std::string exeDir = GetExecutableDir();
    std::vector<std::string> candidates = {
        exeDir + "/libantigravity_proxy.so",
        exeDir + "/../lib/libantigravity_proxy.so",
        exeDir + "/../build/libantigravity_proxy.so",
        "/usr/local/lib/libantigravity_proxy.so",
        "/usr/lib/libantigravity_proxy.so"
    };
    for (const auto& c : candidates) {
        if (FileExists(c)) {
            char resolved[PATH_MAX] = {0};
            if (realpath(c.c_str(), resolved)) {
                return std::string(resolved);
            }
            return c;
        }
    }
    return exeDir + "/libantigravity_proxy.so";
}

std::string FindAgyPath() {
    if (const char* envPath = getenv("ANTIGRAVITY_CLI_PATH")) {
        if (FileExists(envPath)) return envPath;
    }
    std::vector<std::string> candidates = {
        "/root/.local/bin/agy",
        "/usr/local/bin/agy",
        "/usr/bin/agy"
    };
    const char* home = getenv("HOME");
    if (home) {
        candidates.insert(candidates.begin(), std::string(home) + "/.local/bin/agy");
    }
    for (const auto& c : candidates) {
        if (FileExists(c)) return c;
    }
    return "agy";
}

std::string FindIdePath() {
    if (const char* envPath = getenv("ANTIGRAVITY_IDE_PATH")) {
        if (FileExists(envPath)) return envPath;
    }
    std::vector<std::string> candidates = {
        "/usr/bin/antigravity",
        "/usr/local/bin/antigravity",
        "/opt/Antigravity/antigravity",
        "/opt/antigravity/antigravity",
        "/usr/share/antigravity/antigravity"
    };
    const char* home = getenv("HOME");
    if (home) {
        candidates.push_back(std::string(home) + "/.local/share/antigravity/antigravity");
        candidates.push_back(std::string(home) + "/.local/bin/antigravity");
    }
    for (const auto& c : candidates) {
        if (FileExists(c)) return c;
    }
    return "antigravity";
}

void PrintUsage(const char* prog) {
    std::cout << "🚀 Antigravity-Proxy Linux 管理与启动工具\n\n"
              << "用法:\n"
              << "  " << prog << " run <命令> [参数...]     通过代理运行指定命令\n"
              << "  " << prog << " ide [参数...]            通过代理启动 Antigravity IDE (桌面端/编辑器)\n"
              << "  " << prog << " agy [参数...]            通过代理运行 Antigravity CLI (agy)\n"
              << "  " << prog << " env                     输出注入环境变量 (用于 eval \"$(" << prog << " env)\")\n"
              << "  " << prog << " status                  显示当前代理配置与环境状态\n"
              << "  " << prog << " test                    测试代理服务器连通性\n"
              << "  " << prog << " patch-desktop           为 Linux 桌面菜单图标生成/注入代理启动项\n"
              << "  " << prog << " help                    显示此帮助信息\n\n"
              << "示例:\n"
              << "  " << prog << " ide                      # 启动 Antigravity IDE (自动透明走代理)\n"
              << "  " << prog << " agy --version            # 运行 Antigravity CLI\n"
              << "  " << prog << " run curl -I https://www.google.com\n"
              << "  eval \"$(" << prog << " env)\" && antigravity\n";
}

int RunWithProxy(const std::string& soPath, const std::string& cmd, const std::vector<std::string>& args) {
    if (!FileExists(soPath)) {
        std::cerr << "❌ 错误: 未找到代理动态库 " << soPath << "\n"
                  << "请确保已编译项目并将 libantigravity_proxy.so 放置在相应目录中。\n";
        return 1;
    }

    // 设置 LD_PRELOAD
    const char* oldPreload = getenv("LD_PRELOAD");
    std::string newPreload = soPath;
    if (oldPreload && *oldPreload) {
        newPreload = std::string(oldPreload) + ":" + soPath;
    }
    setenv("LD_PRELOAD", newPreload.c_str(), 1);

    std::vector<char*> argv;
    argv.push_back(const_cast<char*>(cmd.c_str()));
    for (const auto& arg : args) {
        argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);

    execvp(cmd.c_str(), argv.data());

    std::cerr << "❌ 启动失败: " << cmd << " (" << strerror(errno) << ")\n"
              << "提示: 若二进制文件不在系统 PATH 中，可通过环境变量指定，例如:\n"
              << "  ANTIGRAVITY_IDE_PATH=/path/to/antigravity antigravity-proxy ide\n";
    return 127;
}

int PatchDesktopEntry(const std::string& soPath) {
    const char* home = getenv("HOME");
    std::string userApps = home ? (std::string(home) + "/.local/share/applications") : "";
    if (userApps.empty()) {
        std::cerr << "❌ 无法获取 HOME 目录\n";
        return 1;
    }
    mkdir(userApps.c_str(), 0755);

    std::string destDesktop = userApps + "/antigravity-proxied.desktop";
    std::string idePath = FindIdePath();

    std::ofstream f(destDesktop);
    if (!f.is_open()) {
        std::cerr << "❌ 无法写入桌面快捷方式: " << destDesktop << "\n";
        return 1;
    }

    f << "[Desktop Entry]\n"
      << "Name=Antigravity (Proxied)\n"
      << "Comment=Antigravity Editor with Transparent Proxy\n"
      << "GenericName=Text Editor\n"
      << "Exec=env LD_PRELOAD=\"" << soPath << "\" " << idePath << " %F\n"
      << "Icon=antigravity\n"
      << "Type=Application\n"
      << "StartupNotify=true\n"
      << "StartupWMClass=Antigravity\n"
      << "Categories=Development;TextEditor;IDE;\n";
    f.close();

    std::cout << "✅ 成功生成带代理的桌面启动项: " << destDesktop << "\n"
              << "现在你可以直接在 Linux 应用启动器中搜索并点击 'Antigravity (Proxied)' 启动。\n";
    return 0;
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        PrintUsage(argv[0]);
        return 0;
    }

    std::string action = argv[1];
    std::string soPath = FindProxySoPath();

    if (action == "help" || action == "--help" || action == "-h") {
        PrintUsage(argv[0]);
        return 0;
    }

    if (action == "env") {
        std::cout << "export LD_PRELOAD=\"" << soPath << "\"\n";
        return 0;
    }

    if (action == "patch-desktop") {
        return PatchDesktopEntry(soPath);
    }

    if (action == "status") {
        std::cout << "=== Antigravity-Proxy Linux 状态 ===\n";
        std::cout << "代理动态库: " << soPath << " [" << (FileExists(soPath) ? "存在" : "未找到") << "]\n";
        std::string agyPath = FindAgyPath();
        std::cout << "检测到 agy: " << agyPath << " [" << (FileExists(agyPath) ? "存在" : "未找到/PATH fallback") << "]\n";
        std::string idePath = FindIdePath();
        std::cout << "检测到 IDE: " << idePath << " [" << (FileExists(idePath) ? "存在" : "未找到/PATH fallback") << "]\n";

        Core::Config::Instance().Load();
        const auto& cfg = Core::Config::Instance();
        std::cout << "\n--- 配置详情 (config.json) ---\n";
        std::cout << "代理类型: " << cfg.proxy.type << "\n";
        std::cout << "代理地址: " << cfg.proxy.host << ":" << cfg.proxy.port << "\n";
        std::cout << "FakeIP:   " << (cfg.fakeIp.enabled ? "启用 (" + cfg.fakeIp.cidr + ")" : "已禁用") << "\n";
        std::cout << "UDP 策略: " << cfg.rules.udp_mode << "\n";
        std::cout << "IPv6 模式: " << cfg.rules.ipv6_mode << "\n";
        std::cout << "日志等级: " << (Core::Logger::IsEnabled(Core::LogLevel::Debug) ? "Debug" : "Info") << "\n";
        return 0;
    }

    if (action == "test") {
        Core::Config::Instance().Load();
        const auto& cfg = Core::Config::Instance();
        std::cout << "🔍 测试代理服务器连通性: " << cfg.proxy.host << ":" << cfg.proxy.port << " ...\n";

        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            std::cerr << "❌ 创建测试 socket 失败\n";
            return 1;
        }

        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(cfg.proxy.port);
        if (inet_pton(AF_INET, cfg.proxy.host.c_str(), &addr.sin_addr) != 1) {
            struct addrinfo hints{}, *res = nullptr;
            hints.ai_family = AF_INET;
            hints.ai_socktype = SOCK_STREAM;
            if (getaddrinfo(cfg.proxy.host.c_str(), nullptr, &hints, &res) != 0 || !res) {
                std::cerr << "❌ 无法解析代理服务器主机名: " << cfg.proxy.host << "\n";
                close(sock);
                return 1;
            }
            addr.sin_addr = reinterpret_cast<sockaddr_in*>(res->ai_addr)->sin_addr;
            freeaddrinfo(res);
        }

        struct timeval tv{};
        tv.tv_sec = 3;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        int rc = connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        if (rc == 0) {
            std::cout << "✅ 成功连接到代理服务器 " << cfg.proxy.host << ":" << cfg.proxy.port << "!\n";
            close(sock);
            return 0;
        } else {
            std::cerr << "⚠️ 无法直接连接到代理服务器 (" << strerror(errno) << ")\n"
                      << "提示: 若尚未启动代理客户端 (如 Clash/V2Ray/Xray/sing-box)，请先启动它并监听对应端口。\n";
            close(sock);
            return 1;
        }
    }

    if (action == "ide") {
        std::string ide = FindIdePath();
        std::vector<std::string> args;
        for (int i = 2; i < argc; ++i) {
            args.push_back(argv[i]);
        }
        return RunWithProxy(soPath, ide, args);
    }

    if (action == "agy") {
        std::string agy = FindAgyPath();
        std::vector<std::string> args;
        for (int i = 2; i < argc; ++i) {
            args.push_back(argv[i]);
        }
        return RunWithProxy(soPath, agy, args);
    }

    if (action == "run") {
        if (argc < 3) {
            std::cerr << "用法: " << argv[0] << " run <命令> [参数...]\n";
            return 1;
        }
        std::string cmd = argv[2];
        std::vector<std::string> args;
        for (int i = 3; i < argc; ++i) {
            args.push_back(argv[i]);
        }
        return RunWithProxy(soPath, cmd, args);
    }

    // 默认作为 run 处理
    std::string cmd = action;
    std::vector<std::string> args;
    for (int i = 2; i < argc; ++i) {
        args.push_back(argv[i]);
    }
    return RunWithProxy(soPath, cmd, args);
}
