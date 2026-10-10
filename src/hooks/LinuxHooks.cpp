#include "../platform/PlatformCompat.hpp"

#ifndef _WIN32

#include <dlfcn.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <limits.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../core/Config.hpp"
#include "../core/Logger.hpp"
#include "../core/UdpPolicy.hpp"
#include "../network/FakeIP.hpp"
#include "../network/HttpConnect.hpp"
#include "../network/LocalTargetPolicy.hpp"
#include "../network/SocketFamily.hpp"
#include "../network/SocketIo.hpp"
#include "../network/SocketWrapper.hpp"
#include "../network/Socks5.hpp"
#include "../network/Socks5Udp.hpp"
#include "../network/TrafficMonitor.hpp"
#include "ProcessName.hpp"

namespace {

// ============= 原生函数指针类型 =============
using connect_t = int (*)(int, const struct sockaddr*, socklen_t);
using getaddrinfo_t = int (*)(const char*, const char*, const struct addrinfo*, struct addrinfo**);
using freeaddrinfo_t = void (*)(struct addrinfo*);
using gethostbyname_t = struct hostent* (*)(const char*);
using send_t = ssize_t (*)(int, const void*, size_t, int);
using recv_t = ssize_t (*)(int, void*, size_t, int);
using sendto_t = ssize_t (*)(int, const void*, size_t, int, const struct sockaddr*, socklen_t);
using recvfrom_t = ssize_t (*)(int, void*, size_t, int, struct sockaddr*, socklen_t*);
using close_t = int (*)(int);
using shutdown_t = int (*)(int, int);
using execve_t = int (*)(const char*, char* const[], char* const[]);
using execv_t = int (*)(const char*, char* const[]);
using execvp_t = int (*)(const char*, char* const[]);
using execvpe_t = int (*)(const char*, char* const[], char* const[]);

static connect_t real_connect = nullptr;
static getaddrinfo_t real_getaddrinfo = nullptr;
static freeaddrinfo_t real_freeaddrinfo = nullptr;
static gethostbyname_t real_gethostbyname = nullptr;
static send_t real_send = nullptr;
static recv_t real_recv = nullptr;
static sendto_t real_sendto = nullptr;
static recvfrom_t real_recvfrom = nullptr;
static close_t real_close = nullptr;
static shutdown_t real_shutdown = nullptr;
static execve_t real_execve = nullptr;
static execv_t real_execv = nullptr;
static execvp_t real_execvp = nullptr;
static execvpe_t real_execvpe = nullptr;

static void InitRealFunctions() {
    static std::once_flag s_initOnce;
    std::call_once(s_initOnce, []() {
        real_connect = (connect_t)dlsym(RTLD_NEXT, "connect");
        real_getaddrinfo = (getaddrinfo_t)dlsym(RTLD_NEXT, "getaddrinfo");
        real_freeaddrinfo = (freeaddrinfo_t)dlsym(RTLD_NEXT, "freeaddrinfo");
        real_gethostbyname = (gethostbyname_t)dlsym(RTLD_NEXT, "gethostbyname");
        real_send = (send_t)dlsym(RTLD_NEXT, "send");
        real_recv = (recv_t)dlsym(RTLD_NEXT, "recv");
        real_sendto = (sendto_t)dlsym(RTLD_NEXT, "sendto");
        real_recvfrom = (recvfrom_t)dlsym(RTLD_NEXT, "recvfrom");
        real_close = (close_t)dlsym(RTLD_NEXT, "close");
        real_shutdown = (shutdown_t)dlsym(RTLD_NEXT, "shutdown");
        real_execve = (execve_t)dlsym(RTLD_NEXT, "execve");
        real_execv = (execv_t)dlsym(RTLD_NEXT, "execv");
        real_execvp = (execvp_t)dlsym(RTLD_NEXT, "execvp");
        real_execvpe = (execvpe_t)dlsym(RTLD_NEXT, "execvpe");
    });
}

// 递归保护（防重入）
thread_local bool t_inHook = false;

struct RecursionGuard {
    bool active = false;
    RecursionGuard() {
        if (!t_inHook) {
            t_inHook = true;
            active = true;
        }
    }
    ~RecursionGuard() {
        if (active) {
            t_inHook = false;
        }
    }
    bool acquired() const { return active; }
};

// ============= 状态跟踪 =============
struct SocketTargetInfo {
    std::string host;
    uint16_t port = 0;
    uint64_t establishedTick = 0;
};

static std::unordered_map<int, SocketTargetInfo> g_socketTargets;
static std::mutex g_socketTargetsMtx;

static std::unordered_set<int> g_localBypassSockets;
static std::shared_mutex g_localBypassSocketsMtx;

static void SetLocalBypassSocket(int s, bool enabled) {
    if (s < 0) return;
    std::unique_lock<std::shared_mutex> lock(g_localBypassSocketsMtx);
    if (enabled) {
        g_localBypassSockets.insert(s);
    } else {
        g_localBypassSockets.erase(s);
    }
}

static bool IsLocalBypassSocket(int s) {
    if (s < 0) return false;
    std::shared_lock<std::shared_mutex> lock(g_localBypassSocketsMtx);
    return g_localBypassSockets.find(s) != g_localBypassSockets.end();
}

static void SaveSocketTarget(int s, const std::string& host, uint16_t port) {
    if (s < 0) return;
    std::lock_guard<std::mutex> lock(g_socketTargetsMtx);
    g_socketTargets[s] = SocketTargetInfo{host, port, GetTickCount64()};
}

static bool TryGetSocketTarget(int s, SocketTargetInfo* out) {
    if (s < 0 || !out) return false;
    std::lock_guard<std::mutex> lock(g_socketTargetsMtx);
    auto it = g_socketTargets.find(s);
    if (it != g_socketTargets.end()) {
        *out = it->second;
        return true;
    }
    return false;
}

static void ForgetSocketTarget(int s) {
    if (s < 0) return;
    std::lock_guard<std::mutex> lock(g_socketTargetsMtx);
    g_socketTargets.erase(s);
}

// ============= 工具函数 =============
static std::string SockaddrToString(const struct sockaddr* addr) {
    if (!addr) return "";
    char ip[INET6_ADDRSTRLEN] = {0};
    uint16_t port = 0;
    if (addr->sa_family == AF_INET) {
        const auto* a4 = reinterpret_cast<const sockaddr_in*>(addr);
        inet_ntop(AF_INET, &a4->sin_addr, ip, sizeof(ip));
        port = ntohs(a4->sin_port);
    } else if (addr->sa_family == AF_INET6) {
        const auto* a6 = reinterpret_cast<const sockaddr_in6*>(addr);
        inet_ntop(AF_INET6, &a6->sin6_addr, ip, sizeof(ip));
        port = ntohs(a6->sin6_port);
    } else {
        return "";
    }
    return std::string(ip) + ":" + std::to_string(port);
}

static bool SockaddrToIp(const struct sockaddr* addr, std::string* outIp, bool* outIsV6) {
    if (!addr || !outIp || !outIsV6) return false;
    char buf[INET6_ADDRSTRLEN] = {0};
    if (addr->sa_family == AF_INET) {
        const auto* a4 = reinterpret_cast<const sockaddr_in*>(addr);
        if (inet_ntop(AF_INET, &a4->sin_addr, buf, sizeof(buf))) {
            *outIp = buf;
            *outIsV6 = false;
            return true;
        }
    } else if (addr->sa_family == AF_INET6) {
        const auto* a6 = reinterpret_cast<const sockaddr_in6*>(addr);
        if (inet_ntop(AF_INET6, &a6->sin6_addr, buf, sizeof(buf))) {
            *outIp = buf;
            *outIsV6 = true;
            return true;
        }
    }
    return false;
}

static bool TryGetSockaddrPort(const struct sockaddr* addr, uint16_t* outPort) {
    if (!addr || !outPort) return false;
    if (addr->sa_family == AF_INET) {
        *outPort = ntohs(reinterpret_cast<const sockaddr_in*>(addr)->sin_port);
        return true;
    }
    if (addr->sa_family == AF_INET6) {
        *outPort = ntohs(reinterpret_cast<const sockaddr_in6*>(addr)->sin6_port);
        return true;
    }
    return false;
}

static bool IsIpLiteralHost(const std::string& host) {
    in_addr a4{};
    if (inet_pton(AF_INET, host.c_str(), &a4) == 1) return true;
    in6_addr a6{};
    if (inet_pton(AF_INET6, host.c_str(), &a6) == 1) return true;
    return false;
}

static bool ResolveOriginalTarget(const struct sockaddr* name, std::string* outHost, uint16_t* outPort) {
    if (!name || !outHost || !outPort) return false;
    if (name->sa_family == AF_INET) {
        const auto* a4 = reinterpret_cast<const sockaddr_in*>(name);
        *outPort = ntohs(a4->sin_port);
        const uint32_t ipNet = a4->sin_addr.s_addr;
        std::string domain = Network::FakeIP::Instance().GetDomain(ipNet);
        if (!domain.empty()) {
            *outHost = domain;
        } else {
            *outHost = Network::FakeIP::IpToString(ipNet);
        }
        return true;
    }
    if (name->sa_family == AF_INET6) {
        const auto* a6 = reinterpret_cast<const sockaddr_in6*>(name);
        *outPort = ntohs(a6->sin6_port);
        if (IN6_IS_ADDR_V4MAPPED(&a6->sin6_addr)) {
            uint32_t ipNet = 0;
            std::memcpy(&ipNet, &a6->sin6_addr.s6_addr[12], 4);
            std::string domain = Network::FakeIP::Instance().GetDomain(ipNet);
            if (!domain.empty()) {
                *outHost = domain;
            } else {
                *outHost = Network::FakeIP::IpToString(ipNet);
            }
            return true;
        }
        char buf[INET6_ADDRSTRLEN] = {0};
        if (inet_ntop(AF_INET6, &a6->sin6_addr, buf, sizeof(buf))) {
            *outHost = buf;
            return true;
        }
    }
    return false;
}

static bool TryResolveDirectTargetFromFakeIp(
    const struct sockaddr* name,
    const std::string& host,
    uint16_t port,
    struct sockaddr_storage* outAddr,
    socklen_t* outLen,
    bool* outWasFake)
{
    if (outWasFake) *outWasFake = false;
    if (!name || !outAddr || !outLen) return false;

    bool isFake = false;
    if (name->sa_family == AF_INET) {
        const auto* a4 = reinterpret_cast<const sockaddr_in*>(name);
        isFake = Network::FakeIP::Instance().IsFakeIP(a4->sin_addr.s_addr);
    } else if (name->sa_family == AF_INET6) {
        const auto* a6 = reinterpret_cast<const sockaddr_in6*>(name);
        if (IN6_IS_ADDR_V4MAPPED(&a6->sin6_addr)) {
            uint32_t ipNet = 0;
            std::memcpy(&ipNet, &a6->sin6_addr.s6_addr[12], 4);
            isFake = Network::FakeIP::Instance().IsFakeIP(ipNet);
        }
    }

    if (!isFake) return false;
    if (outWasFake) *outWasFake = true;

    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    if (!real_getaddrinfo) InitRealFunctions();
    int rc = real_getaddrinfo ? real_getaddrinfo(host.c_str(), nullptr, &hints, &res) : EAI_FAIL;
    if (rc != 0 || !res) return false;

    std::memset(outAddr, 0, sizeof(*outAddr));
    if (res->ai_family == AF_INET) {
        auto* dst = reinterpret_cast<sockaddr_in*>(outAddr);
        *dst = *reinterpret_cast<sockaddr_in*>(res->ai_addr);
        dst->sin_port = htons(port);
        *outLen = sizeof(sockaddr_in);
    } else if (res->ai_family == AF_INET6) {
        auto* dst = reinterpret_cast<sockaddr_in6*>(outAddr);
        *dst = *reinterpret_cast<sockaddr_in6*>(res->ai_addr);
        dst->sin6_port = htons(port);
        *outLen = sizeof(sockaddr_in6);
    } else {
        if (real_freeaddrinfo) real_freeaddrinfo(res);
        return false;
    }
    if (real_freeaddrinfo) real_freeaddrinfo(res);
    return true;
}

static bool IsProxySelfTarget(const std::string& host, uint16_t port, const Core::ProxyConfig& proxy) {
    if (port != proxy.port) return false;
    if (host == proxy.host) return true;
    if (Network::IsLoopbackHost(host) && Network::IsLoopbackHost(proxy.host)) return true;
    return false;
}

static bool BuildProxyAddr(const Core::ProxyConfig& proxy, sockaddr_in* outAddr) {
    if (!outAddr) return false;
    std::memset(outAddr, 0, sizeof(*outAddr));
    outAddr->sin_family = AF_INET;
    outAddr->sin_port = htons(proxy.port);

    if (inet_pton(AF_INET, proxy.host.c_str(), &outAddr->sin_addr) == 1) {
        return true;
    }
    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    if (!real_getaddrinfo) InitRealFunctions();
    int rc = real_getaddrinfo ? real_getaddrinfo(proxy.host.c_str(), nullptr, &hints, &res) : EAI_FAIL;
    if (rc != 0 || !res) return false;

    outAddr->sin_addr = reinterpret_cast<sockaddr_in*>(res->ai_addr)->sin_addr;
    if (real_freeaddrinfo) real_freeaddrinfo(res);
    return true;
}

static bool BuildProxyAddrV6(const Core::ProxyConfig& proxy, sockaddr_in6* outAddr6) {
    if (!outAddr6) return false;
    std::memset(outAddr6, 0, sizeof(*outAddr6));
    outAddr6->sin6_family = AF_INET6;
    outAddr6->sin6_port = htons(proxy.port);

    if (inet_pton(AF_INET6, proxy.host.c_str(), &outAddr6->sin6_addr) == 1) {
        return true;
    }

    sockaddr_in proxy4{};
    if (BuildProxyAddr(proxy, &proxy4)) {
        // v4-mapped IPv6
        outAddr6->sin6_addr.s6_addr[10] = 0xff;
        outAddr6->sin6_addr.s6_addr[11] = 0xff;
        std::memcpy(&outAddr6->sin6_addr.s6_addr[12], &proxy4.sin_addr, 4);
        return true;
    }
    return false;
}

static bool DoProxyHandshake(int s, const std::string& targetHost, uint16_t targetPort) {
    const auto& config = Core::Config::Instance();
    std::string proxyType = config.proxy.type;
    std::transform(proxyType.begin(), proxyType.end(), proxyType.begin(), [](unsigned char c) { return std::tolower(c); });

    bool ok = false;
    if (proxyType == "http") {
        ok = Network::HttpConnectClient::Handshake(s, targetHost, targetPort);
    } else {
        ok = Network::Socks5Client::Handshake(s, targetHost, targetPort);
    }

    if (ok) {
        SaveSocketTarget(s, targetHost, targetPort);
        Core::Logger::Info("代理连接成功建立: " + targetHost + ":" + std::to_string(targetPort) + " (fd=" + std::to_string(s) + ")");
    } else {
        Core::Logger::Error("代理握手失败: " + targetHost + ":" + std::to_string(targetPort) + " (fd=" + std::to_string(s) + ")");
    }
    return ok;
}

// 执行核心代理连接决策
static int PerformProxyConnect(int s, const struct sockaddr* name, socklen_t namelen) {
    if (!real_connect) InitRealFunctions();
    auto& config = Core::Config::Instance();

    if (!name || namelen < sizeof(struct sockaddr)) {
        errno = EINVAL;
        return -1;
    }

    // 回环连接早期旁路
    if (Network::IsLoopbackSockaddr(name, namelen)) {
        SetLocalBypassSocket(s, true);
        return real_connect(s, name, namelen);
    }
    SetLocalBypassSocket(s, false);

    // 仅代理 SOCK_STREAM (TCP)
    int soType = 0;
    socklen_t optLen = sizeof(soType);
    if (getsockopt(s, SOL_SOCKET, SO_TYPE, &soType, &optLen) == 0) {
        if (soType != SOCK_STREAM) {
            if (soType == SOCK_DGRAM) {
                const Core::UdpAction udpAction = Core::ResolveUdpAction(
                    config.rules.udp_mode, config.proxy.type, config.rules.udp_fallback);
                if (udpAction == Core::UdpAction::Block) {
                    uint16_t dstPort = 0;
                    TryGetSockaddrPort(name, &dstPort);
                    if (dstPort != 53 && !Network::IsLoopbackSockaddr(name, namelen)) {
                        Core::Logger::Warn("已阻止 UDP 连接(策略: block), fd=" + std::to_string(s));
                        errno = EACCES;
                        return -1;
                    }
                }
            }
            return real_connect(s, name, namelen);
        }
    }

    std::string originalHost;
    uint16_t originalPort = 0;
    if (!ResolveOriginalTarget(name, &originalHost, &originalPort)) {
        return real_connect(s, name, namelen);
    }

    // 回环 bypass
    if (Network::IsLoopbackHost(originalHost)) {
        SetLocalBypassSocket(s, true);
        return real_connect(s, name, namelen);
    }

    // 代理自连接 bypass
    if (IsProxySelfTarget(originalHost, originalPort, config.proxy)) {
        return real_connect(s, name, namelen);
    }

    // 路由规则检查
    std::string addrIp;
    bool addrIsV6 = false;
    SockaddrToIp(name, &addrIp, &addrIsV6);
    std::string routeAction;
    std::string routeRule;
    bool routeMatched = config.rules.MatchRouting(originalHost, addrIp, addrIsV6, originalPort, "tcp", &routeAction, &routeRule);

    if (routeAction == "direct") {
        sockaddr_storage realAddr{};
        socklen_t realLen = 0;
        bool wasFake = false;
        if (TryResolveDirectTargetFromFakeIp(name, originalHost, originalPort, &realAddr, &realLen, &wasFake)) {
            Core::Logger::Info("[Route] direct: FakeIP 已重解析直连, target=" + originalHost + ":" + std::to_string(originalPort));
            return real_connect(s, reinterpret_cast<sockaddr*>(&realAddr), realLen);
        }
        return real_connect(s, name, namelen);
    }

    // DNS 53 端口处理
    if (originalPort == 53 && (config.rules.dns_mode == "direct" || config.rules.dns_mode.empty())) {
        return real_connect(s, name, namelen);
    }

    // 端口白名单过滤
    if (!config.rules.IsPortAllowed(originalPort)) {
        return real_connect(s, name, namelen);
    }

    // 连接代理服务器
    if (config.proxy.port != 0) {
        Core::Logger::Info("重定向连接到代理: " + originalHost + ":" + std::to_string(originalPort) +
                           " -> " + config.proxy.host + ":" + std::to_string(config.proxy.port));

        Network::SocketWrapper sockWrap(s);
        sockWrap.SetTimeouts(config.timeout.recv_ms, config.timeout.send_ms);

        int result = 0;
        if (name->sa_family == AF_INET6) {
            sockaddr_in6 proxyAddr6{};
            if (!BuildProxyAddrV6(config.proxy, &proxyAddr6)) {
                errno = EINVAL;
                return -1;
            }
            Network::EnsureIpv6DualStack(s, AF_INET6);
            result = real_connect(s, reinterpret_cast<sockaddr*>(&proxyAddr6), sizeof(proxyAddr6));
        } else {
            sockaddr_in proxyAddr4{};
            if (!BuildProxyAddr(config.proxy, &proxyAddr4)) {
                errno = EINVAL;
                return -1;
            }
            result = real_connect(s, reinterpret_cast<sockaddr*>(&proxyAddr4), sizeof(proxyAddr4));
        }

        if (result != 0) {
            if (errno == EINPROGRESS || errno == EWOULDBLOCK) {
                if (!Network::SocketIo::WaitConnect(s, config.timeout.connect_ms)) {
                    Core::Logger::Error("连接代理服务器超时或失败, fd=" + std::to_string(s) + ", errno=" + std::to_string(errno));
                    return -1;
                }
            } else {
                Core::Logger::Error("连接代理服务器失败, fd=" + std::to_string(s) + ", errno=" + std::to_string(errno));
                return result;
            }
        }

        if (!DoProxyHandshake(s, originalHost, originalPort)) {
            return -1;
        }
        return 0;
    }

    return real_connect(s, name, namelen);
}

// 获取当前动态库绝对路径，用于 child injection
static std::string GetCurrentLibraryPath() {
    Dl_info info;
    if (dladdr(reinterpret_cast<void*>(&InitRealFunctions), &info) && info.dli_fname) {
        char resolved[PATH_MAX] = {0};
        if (realpath(info.dli_fname, resolved)) {
            return std::string(resolved);
        }
        return std::string(info.dli_fname);
    }
    return "";
}

// 检查并准备子进程的环境变量（确保 LD_PRELOAD 传递或排除）
static std::vector<char*> PrepareChildEnvironment(const char* file, char* const envp[]) {
    auto& config = Core::Config::Instance();
    std::string baseName = Hooks::ExtractBaseNameFromPathLike(file ? file : "");
    bool inject = config.childInjection && config.ShouldInjectChildProcess(baseName);

    std::string libPath = GetCurrentLibraryPath();
    std::vector<std::string> newEnvStrings;
    bool foundPreload = false;

    // 读取现有 envp 或系统 environ
    char* const* curEnv = envp ? envp : environ;
    if (curEnv) {
        for (size_t i = 0; curEnv[i] != nullptr; ++i) {
            std::string entry = curEnv[i];
            if (entry.rfind("LD_PRELOAD=", 0) == 0) {
                foundPreload = true;
                if (!inject) {
                    // 排除此进程：不添加原 LD_PRELOAD
                    continue;
                }
                // 如果需要注入且没有此库，追加进去
                if (entry.find(libPath) == std::string::npos) {
                    entry = entry + ":" + libPath;
                }
                newEnvStrings.push_back(entry);
            } else {
                newEnvStrings.push_back(entry);
            }
        }
    }

    if (inject && !foundPreload && !libPath.empty()) {
        newEnvStrings.push_back("LD_PRELOAD=" + libPath);
    }

    std::vector<char*> result;
    result.reserve(newEnvStrings.size() + 1);
    for (const auto& s : newEnvStrings) {
        result.push_back(strdup(s.c_str()));
    }
    result.push_back(nullptr);
    return result;
}

static void FreeChildEnvironment(std::vector<char*>& envVec) {
    for (char* p : envVec) {
        if (p) free(p);
    }
    envVec.clear();
}

} // anonymous namespace

// ============= 导出的 Linux Hook API =============

extern "C" {

AGY_EXPORT int connect(int sockfd, const struct sockaddr* addr, socklen_t addrlen) {
    if (!real_connect) InitRealFunctions();
    RecursionGuard guard;
    if (!guard.acquired()) {
        return real_connect ? real_connect(sockfd, addr, addrlen) : -1;
    }
    return PerformProxyConnect(sockfd, addr, addrlen);
}

AGY_EXPORT int getaddrinfo(const char* node, const char* service, const struct addrinfo* hints, struct addrinfo** res) {
    if (!real_getaddrinfo) InitRealFunctions();
    RecursionGuard guard;
    if (!guard.acquired()) {
        return real_getaddrinfo ? real_getaddrinfo(node, service, hints, res) : EAI_FAIL;
    }

    auto& config = Core::Config::Instance();
    if (node && config.fakeIp.enabled) {
        std::string host = node;
        if (!host.empty() && !Network::IsLoopbackHost(host) && !IsIpLiteralHost(host)) {
            // 路由策略检查
            std::string routeAction;
            std::string routeRule;
            uint16_t port = 0;
            if (service) {
                try { port = static_cast<uint16_t>(std::stoi(service)); } catch (...) {}
            }
            bool matched = config.rules.MatchRouting(host, "", false, port, "tcp", &routeAction, &routeRule);
            if (routeAction == "direct") {
                return real_getaddrinfo(node, service, hints, res);
            }

            uint32_t fakeIp = Network::FakeIP::Instance().Alloc(host);
            if (fakeIp != 0) {
                std::string fakeIpStr = Network::FakeIP::IpToString(fakeIp);
                int family = hints ? hints->ai_family : AF_UNSPEC;
                std::string fakeNode = fakeIpStr;
                if (family == AF_INET6) {
                    fakeNode = "::ffff:" + fakeIpStr;
                } else if (family != AF_UNSPEC && family != AF_INET) {
                    return real_getaddrinfo(node, service, hints, res);
                }
                int rc = real_getaddrinfo(fakeNode.c_str(), service, hints, res);
                if (rc == 0) return rc;
            }
        }
    }
    return real_getaddrinfo ? real_getaddrinfo(node, service, hints, res) : EAI_FAIL;
}

AGY_EXPORT void freeaddrinfo(struct addrinfo* res) {
    if (!real_freeaddrinfo) InitRealFunctions();
    if (real_freeaddrinfo) real_freeaddrinfo(res);
}

AGY_EXPORT struct hostent* gethostbyname(const char* name) {
    if (!real_gethostbyname) InitRealFunctions();
    RecursionGuard guard;
    if (!guard.acquired()) {
        return real_gethostbyname ? real_gethostbyname(name) : nullptr;
    }

    auto& config = Core::Config::Instance();
    if (name && config.fakeIp.enabled) {
        std::string host = name;
        if (!host.empty() && !Network::IsLoopbackHost(host) && !IsIpLiteralHost(host)) {
            uint32_t fakeIp = Network::FakeIP::Instance().Alloc(host);
            if (fakeIp != 0) {
                static thread_local hostent s_he{};
                static thread_local char s_ipBuf[4];
                static thread_local char* s_addrList[2];
                static thread_local std::string s_nameBuf;
                s_nameBuf = host;
                s_he.h_name = const_cast<char*>(s_nameBuf.c_str());
                s_he.h_aliases = nullptr;
                s_he.h_addrtype = AF_INET;
                s_he.h_length = 4;
                s_addrList[0] = s_ipBuf;
                s_addrList[1] = nullptr;
                std::memcpy(s_ipBuf, &fakeIp, 4);
                s_he.h_addr_list = s_addrList;
                return &s_he;
            }
        }
    }
    return real_gethostbyname ? real_gethostbyname(name) : nullptr;
}

AGY_EXPORT ssize_t send(int sockfd, const void* buf, size_t len, int flags) {
    if (!real_send) InitRealFunctions();
    if (Core::Config::Instance().trafficLogging) {
        Network::TrafficMonitor::Instance().LogSend(sockfd, static_cast<const char*>(buf), static_cast<int>(len));
    }
    return real_send ? real_send(sockfd, buf, len, flags) : -1;
}

AGY_EXPORT ssize_t recv(int sockfd, void* buf, size_t len, int flags) {
    if (!real_recv) InitRealFunctions();
    ssize_t ret = real_recv ? real_recv(sockfd, buf, len, flags) : -1;
    if (ret > 0 && Core::Config::Instance().trafficLogging) {
        Network::TrafficMonitor::Instance().LogRecv(sockfd, static_cast<const char*>(buf), static_cast<int>(ret));
    }
    return ret;
}

AGY_EXPORT ssize_t sendto(int sockfd, const void* buf, size_t len, int flags, const struct sockaddr* dest_addr, socklen_t addrlen) {
    if (!real_sendto) InitRealFunctions();
    return real_sendto ? real_sendto(sockfd, buf, len, flags, dest_addr, addrlen) : -1;
}

AGY_EXPORT ssize_t recvfrom(int sockfd, void* buf, size_t len, int flags, struct sockaddr* src_addr, socklen_t* addrlen) {
    if (!real_recvfrom) InitRealFunctions();
    return real_recvfrom ? real_recvfrom(sockfd, buf, len, flags, src_addr, addrlen) : -1;
}

AGY_EXPORT int close(int fd) {
    if (!real_close) InitRealFunctions();
    ForgetSocketTarget(fd);
    SetLocalBypassSocket(fd, false);
    return real_close ? real_close(fd) : -1;
}

AGY_EXPORT int shutdown(int sockfd, int how) {
    if (!real_shutdown) InitRealFunctions();
    return real_shutdown ? real_shutdown(sockfd, how) : -1;
}

AGY_EXPORT int execve(const char* filename, char* const argv[], char* const envp[]) {
    if (!real_execve) InitRealFunctions();
    std::vector<char*> newEnv = PrepareChildEnvironment(filename, envp);
    int rc = real_execve(filename, argv, newEnv.data());
    FreeChildEnvironment(newEnv);
    return rc;
}

AGY_EXPORT int execv(const char* path, char* const argv[]) {
    if (!real_execv) InitRealFunctions();
    std::vector<char*> newEnv = PrepareChildEnvironment(path, environ);
    int rc = real_execve(path, argv, newEnv.data());
    FreeChildEnvironment(newEnv);
    return rc;
}

AGY_EXPORT int execvp(const char* file, char* const argv[]) {
    if (!real_execvp) InitRealFunctions();
    std::vector<char*> newEnv = PrepareChildEnvironment(file, environ);
    if (!real_execvpe) real_execvpe = (execvpe_t)dlsym(RTLD_NEXT, "execvpe");
    if (real_execvpe) {
        int rc = real_execvpe(file, argv, newEnv.data());
        FreeChildEnvironment(newEnv);
        return rc;
    }
    FreeChildEnvironment(newEnv);
    return real_execvp(file, argv);
}

AGY_EXPORT int execvpe(const char* file, char* const argv[], char* const envp[]) {
    if (!real_execvpe) InitRealFunctions();
    std::vector<char*> newEnv = PrepareChildEnvironment(file, envp);
    if (real_execvpe) {
        int rc = real_execvpe(file, argv, newEnv.data());
        FreeChildEnvironment(newEnv);
        return rc;
    }
    FreeChildEnvironment(newEnv);
    return -1;
}

} // extern "C"

#endif // !_WIN32
