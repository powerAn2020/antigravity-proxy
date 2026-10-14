#include "platform/PlatformCompat.hpp"

#ifndef _WIN32

#include <unistd.h>
#include <limits.h>
#include <string>
#include <iostream>

#include "core/Config.hpp"
#include "core/Logger.hpp"
#include "hooks/ProcessName.hpp"
#include "update/UpdateChecker.hpp"

namespace {

std::string GetCurrentProcessPath() {
    char buf[PATH_MAX] = {0};
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len > 0) {
        buf[len] = '\0';
        return std::string(buf);
    }
    return "unknown";
}

} // namespace

__attribute__((constructor))
static void AntigravityProxyInit() {
    // 0. 若当前进程为 Chromium 沙箱子进程（如渲染器/GPU进程），完全放行，跳过所有代理初始化
    if (Hooks::IsCurrentProcessChromiumSandboxed()) {
        return;
    }

    // 1. 加载配置
    bool loaded = Core::Config::Instance().Load("config.json");

    std::string procPath = GetCurrentProcessPath();
    std::string procName = Hooks::ExtractBaseNameFromPathLike(procPath);

    const auto& config = Core::Config::Instance();

    // 自动为 Go 目标程序 (如 language_server、agy) 设置标准代理环境变量
    // 由于 Go 运行时底层直接使用汇编系统调用绕过 glibc，需由环境变量指导其内部 HTTP/gRPC 客户端走代理
    if (config.proxy.port != 0) {
        bool isGoTarget = (procName.find("language_server") != std::string::npos ||
                           procName.find("agy") != std::string::npos);
        if (isGoTarget) {
            std::string scheme = config.proxy.type.empty() ? "socks5" : config.proxy.type;
            std::string proxyUrl = scheme + "://" + config.proxy.host + ":" + std::to_string(config.proxy.port);
            setenv("ALL_PROXY", proxyUrl.c_str(), 0);
            setenv("all_proxy", proxyUrl.c_str(), 0);
            setenv("HTTP_PROXY", proxyUrl.c_str(), 0);
            setenv("http_proxy", proxyUrl.c_str(), 0);
            setenv("HTTPS_PROXY", proxyUrl.c_str(), 0);
            setenv("https_proxy", proxyUrl.c_str(), 0);
            setenv("NO_PROXY", "localhost,127.0.0.1,::1", 0);
            setenv("no_proxy", "localhost,127.0.0.1,::1", 0);
        }
    }

    // 2. 日志记录
    if (Core::Logger::IsEnabled(Core::LogLevel::Info)) {
        std::string modeDesc = (config.proxy.port != 0) 
            ? (config.proxy.type + " 代理 " + config.proxy.host + ":" + std::to_string(config.proxy.port))
            : "直连模式";

        Core::Logger::Info("Antigravity-Proxy (Linux aarch64/x86_64) 已加载");
        Core::Logger::Info("当前进程: " + procName + " (路径: " + procPath + ", PID: " + std::to_string(getpid()) + ")");
        Core::Logger::Info("运行配置: " + modeDesc + ", FakeIP=" + (config.fakeIp.enabled ? "启用" : "关闭"));
    }

    // 3. 异步更新检查 (默认关闭)
    UpdateChecker::StartAsync();
}

__attribute__((destructor))
static void AntigravityProxyFini() {
    if (Hooks::IsCurrentProcessChromiumSandboxed()) {
        return;
    }
    if (Core::Logger::IsEnabled(Core::LogLevel::Debug)) {
        Core::Logger::Debug("Antigravity-Proxy 正在卸载 (PID: " + std::to_string(getpid()) + ")");
    }
}

#endif // !_WIN32
