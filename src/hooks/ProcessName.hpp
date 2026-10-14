#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <utility>
#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#include <sys/syscall.h>
#endif

namespace Hooks {

inline std::string ToLowerAsciiCopy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

inline void TrimAsciiInPlace(std::string& s) {
    auto isWs = [](unsigned char c) { return std::isspace(c) != 0; };
    size_t begin = 0;
    while (begin < s.size() && isWs(static_cast<unsigned char>(s[begin]))) begin++;
    size_t end = s.size();
    while (end > begin && isWs(static_cast<unsigned char>(s[end - 1]))) end--;
    s = s.substr(begin, end - begin);
}

inline std::string StripOuterQuotesCopy(std::string s) {
    TrimAsciiInPlace(s);
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
        s = s.substr(1, s.size() - 2);
        TrimAsciiInPlace(s);
    }
    return s;
}

inline std::string ExtractBaseNameFromPathLike(std::string s) {
    s = StripOuterQuotesCopy(std::move(s));
    const size_t lastSlash = s.find_last_of("\\/");
    if (lastSlash != std::string::npos) {
        s = s.substr(lastSlash + 1);
    }
    return StripOuterQuotesCopy(std::move(s));
}

inline std::string ExtractExecutableTokenFromCommandLine(std::string commandLine) {
    commandLine = StripOuterQuotesCopy(std::move(commandLine));
    if (commandLine.empty()) return "";

    // CreateProcess 的命令行可能是 `"C:\...\Antigravity IDE.exe" --flag`。
    // 先保留引号内完整路径，避免把带空格的 exe 名截断成 `Antigravity`。
    if (commandLine.front() == '"') {
        const size_t closingQuote = commandLine.find('"', 1);
        if (closingQuote != std::string::npos) {
            return commandLine.substr(1, closingQuote - 1);
        }
    }

    const std::string lowerCommandLine = ToLowerAsciiCopy(commandLine);
    const size_t exePos = lowerCommandLine.find(".exe");
    if (exePos != std::string::npos) {
        return commandLine.substr(0, exePos + 4);
    }

    const size_t firstSpace = commandLine.find(' ');
    if (firstSpace != std::string::npos) {
        return commandLine.substr(0, firstSpace);
    }
    return commandLine;
}

inline std::string GetCreateProcessTargetBaseNameA(const char* applicationName, const char* commandLine) {
    std::string target;
    if (applicationName && *applicationName) {
        target = applicationName;
    } else if (commandLine && *commandLine) {
        target = ExtractExecutableTokenFromCommandLine(commandLine);
    }
    target = ExtractBaseNameFromPathLike(std::move(target));
    return target.empty() ? std::string("Unknown") : target;
}

inline bool IsLanguageServerProcessName(const std::string& processName) {
    const std::string lowerName = ToLowerAsciiCopy(processName);
    return lowerName == "language_server.exe" ||
           lowerName == "language_server" ||
           lowerName.find("language_server_windows") != std::string::npos;
}

inline bool IsAntigravityHostProcessName(const std::string& processName) {
    const std::string lowerName = ToLowerAsciiCopy(processName);
    return lowerName == "antigravity.exe" ||
           lowerName == "antigravity ide.exe";
}

#ifndef _WIN32
inline bool IsChromiumSandboxedProcessType(std::string_view typeArg) {
    // 检查是否为已知被 seccomp-bpf 严格沙箱化的子进程类型
    // 这些进程严禁触发未授权网络系统调用（如 getsockopt, socket 等），
    // 且其网络请求完全由 Chromium 浏览器/网络主进程统筹中转，绝不应注入代理或拦截
    return typeArg.rfind("--type=renderer", 0) == 0 ||
           typeArg.rfind("--type=gpu-process", 0) == 0 ||
           typeArg.rfind("--type=zygote", 0) == 0 ||
           typeArg.rfind("--type=crashpad-handler", 0) == 0;
}

inline bool IsCurrentProcessChromiumSandboxed() {
    static int s_isSandboxed = -1;
    if (s_isSandboxed != -1) return s_isSandboxed == 1;

    // 先设为 0，防止内部调用时发生任何重入递归
    s_isSandboxed = 0;

    int fd = static_cast<int>(::syscall(SYS_openat, AT_FDCWD, "/proc/self/cmdline", O_RDONLY | O_CLOEXEC));
    if (fd >= 0) {
        char buf[4096] = {0};
        ssize_t n = ::syscall(SYS_read, fd, buf, sizeof(buf) - 1);
        ::syscall(SYS_close, fd);
        if (n > 0) {
            const char* p = buf;
            while (p < buf + n) {
                std::string_view arg(p);
                if (IsChromiumSandboxedProcessType(arg)) {
                    s_isSandboxed = 1;
                    return true;
                }
                p += arg.size() + 1;
            }
        }
    }
    return false;
}

inline bool IsChildArgvChromiumSandboxed(char* const argv[]) {
    if (!argv) return false;
    for (size_t i = 0; argv[i] != nullptr; ++i) {
        if (IsChromiumSandboxedProcessType(argv[i])) {
            return true;
        }
    }
    return false;
}
#endif

} // namespace Hooks
