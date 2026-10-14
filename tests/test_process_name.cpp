// 回归测试在 Release 构建下也必须执行断言。
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <string>

#include "hooks/ProcessName.hpp"

int main() {
    using Hooks::GetCreateProcessTargetBaseNameA;
    using Hooks::IsAntigravityHostProcessName;
    using Hooks::IsLanguageServerProcessName;

    // lpApplicationName 明确给出路径时，不按空格截断文件名。
    assert(GetCreateProcessTargetBaseNameA(
               "C:\\Users\\test\\AppData\\Local\\Programs\\Antigravity\\Antigravity IDE.exe",
               nullptr) == "Antigravity IDE.exe");

    // 命令行带引号时，保留引号内完整 exe 路径，再提取文件名。
    assert(GetCreateProcessTargetBaseNameA(
               nullptr,
               "\"C:\\Users\\test\\AppData\\Local\\Programs\\Antigravity\\Antigravity IDE.exe\" --type=renderer") ==
           "Antigravity IDE.exe");

    // 命令行未加引号但包含 .exe 时，优先截到 .exe，避免参数污染进程名。
    assert(GetCreateProcessTargetBaseNameA(
               nullptr,
               "C:\\Users\\test\\AppData\\Local\\Programs\\Antigravity\\resources\\bin\\language_server.exe --stdio") ==
           "language_server.exe");

    // 普通无空格路径保持历史行为。
    assert(GetCreateProcessTargetBaseNameA(nullptr, "node.exe --inspect") == "node.exe");

    // 新旧 language server 命名都要触发窄范围 node 子进程兼容。
    assert(IsLanguageServerProcessName("language_server.exe"));
    assert(IsLanguageServerProcessName("language_server"));
    assert(IsLanguageServerProcessName("language_server_windows_x64.exe"));
    assert(!IsLanguageServerProcessName("Antigravity IDE.exe"));

    // 保留宿主进程名分类工具的稳定行为；运行时策略在 main.cpp 中统一启用全量 Hook。
    assert(IsAntigravityHostProcessName("Antigravity.exe"));
    assert(IsAntigravityHostProcessName("ANTIGRAVITY IDE.EXE"));
    assert(!IsAntigravityHostProcessName("language_server.exe"));
    assert(!IsAntigravityHostProcessName("node.exe"));

#ifndef _WIN32
    assert(Hooks::IsChromiumSandboxedProcessType("--type=renderer"));
    assert(Hooks::IsChromiumSandboxedProcessType("--type=gpu-process"));
    assert(Hooks::IsChromiumSandboxedProcessType("--type=zygote"));
    assert(Hooks::IsChromiumSandboxedProcessType("--type=crashpad-handler"));
    assert(!Hooks::IsChromiumSandboxedProcessType("--type=utility"));
    assert(!Hooks::IsChromiumSandboxedProcessType("antigravity-ide"));

    char* testArgvRenderer[] = { const_cast<char*>("antigravity-ide"), const_cast<char*>("--type=renderer"), nullptr };
    assert(Hooks::IsChildArgvChromiumSandboxed(testArgvRenderer));

    char* testArgvUtility[] = { const_cast<char*>("antigravity-ide"), const_cast<char*>("--type=utility"), nullptr };
    assert(!Hooks::IsChildArgvChromiumSandboxed(testArgvUtility));
#endif

    return 0;
}
