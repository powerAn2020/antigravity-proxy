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
    // 1. 加载配置
    bool loaded = Core::Config::Instance().Load("config.json");

    std::string procPath = GetCurrentProcessPath();
    std::string procName = Hooks::ExtractBaseNameFromPathLike(procPath);

    const auto& config = Core::Config::Instance();

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
    if (Core::Logger::IsEnabled(Core::LogLevel::Debug)) {
        Core::Logger::Debug("Antigravity-Proxy 正在卸载 (PID: " + std::to_string(getpid()) + ")");
    }
}

#endif // !_WIN32
