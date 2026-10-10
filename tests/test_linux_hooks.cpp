#include "platform/PlatformCompat.hpp"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include <cstring>

#include "core/Config.hpp"
#include "core/Logger.hpp"
#include "network/FakeIP.hpp"
#include "network/LocalTargetPolicy.hpp"
#include "network/SocketFamily.hpp"
#include "hooks/ProcessName.hpp"

int main() {
    std::cout << "[Test] Running Linux integration tests...\n";

    // 1. Process name extraction
    assert(Hooks::ExtractBaseNameFromPathLike("/root/.local/bin/agy") == "agy");
    assert(Hooks::ExtractBaseNameFromPathLike("/usr/bin/language_server") == "language_server");
    assert(Hooks::ExtractBaseNameFromPathLike("/opt/Antigravity/antigravity") == "antigravity");

    // 2. Local loopback policy
    assert(Network::IsLoopbackHost("localhost"));
    assert(Network::IsLoopbackHost("127.0.0.1"));
    assert(Network::IsLoopbackHost("::1"));
    assert(!Network::IsLoopbackHost("www.google.com"));

    // 3. FakeIP ring buffer allocation
    auto& fakeIp = Network::FakeIP::Instance();
    uint32_t ip1 = fakeIp.Alloc("api.github.com");
    assert(ip1 != 0);
    assert(fakeIp.IsFakeIP(ip1));
    std::string domain1 = fakeIp.GetDomain(ip1);
    assert(domain1 == "api.github.com");

    uint32_t ip2 = fakeIp.Alloc("oauth.antigravity.dev");
    assert(ip2 != 0);
    assert(fakeIp.IsFakeIP(ip2));
    assert(fakeIp.GetDomain(ip2) == "oauth.antigravity.dev");
    assert(ip1 != ip2);

    // Idempotent allocation
    uint32_t ip1Again = fakeIp.Alloc("api.github.com");
    assert(ip1 == ip1Again);

    // 4. Config loading & target process matching
    auto& config = Core::Config::Instance();
    config.targetProcesses = {"agy", "language_server", "antigravity.exe", "node"};
    assert(config.ShouldInject("agy"));
    assert(config.ShouldInject("language_server"));
    assert(config.ShouldInject("antigravity")); // matches antigravity.exe without .exe
    assert(config.ShouldInject("antigravity.exe"));
    assert(config.ShouldInject("node"));
    assert(!config.ShouldInject("curl"));

    // 5. Dual-stack check
    SOCKET s6 = socket(AF_INET6, SOCK_STREAM, 0);
    if (s6 >= 0) {
        int err = 0;
        Network::DualStackResult dsRes = Network::EnsureIpv6DualStack(s6, AF_INET6, &err);
        assert(dsRes == Network::DualStackResult::Enabled || dsRes == Network::DualStackResult::AlreadyEnabled);
        closesocket(s6);
    }

    std::cout << "✅ [Test] All Linux integration tests passed successfully!\n";
    return 0;
}
