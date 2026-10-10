#pragma once

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <mswsock.h>

#define AGY_EXPORT __declspec(dllexport)
using socklen_compat_t = int;

#else // Non-Windows (Linux / POSIX)

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/select.h>
#include <sys/time.h>
#include <errno.h>
#include <dlfcn.h>
#include <dirent.h>
#include <pthread.h>
#include <ctime>
#include <cstring>
#include <cstdlib>
#include <cstdint>

#define AGY_EXPORT __attribute__((visibility("default")))
using socklen_compat_t = socklen_t;

using SOCKET = int;
constexpr SOCKET INVALID_SOCKET = -1;
constexpr int SOCKET_ERROR = -1;

#ifndef WSAAPI
#define WSAAPI
#endif

#ifndef WINAPI
#define WINAPI
#endif

#ifndef MAX_PATH
#define MAX_PATH 4096
#endif

using DWORD = uint32_t;
using WORD = uint16_t;
using BYTE = uint8_t;
using BOOL = int;
using ULONGLONG = uint64_t;
using HANDLE = void*;
using LPVOID = void*;
using LPCSTR = const char*;
using PCSTR = const char*;

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

inline int WSAGetLastError() { return errno; }
inline void WSASetLastError(int err) { errno = err; }
inline int closesocket(SOCKET s) { return ::close(s); }

// Windows Sockets Error Codes mapped to POSIX errno
constexpr int WSAEWOULDBLOCK = EWOULDBLOCK;
constexpr int WSAEINPROGRESS = EINPROGRESS;
constexpr int WSAEALREADY = EALREADY;
constexpr int WSAETIMEDOUT = ETIMEDOUT;
constexpr int WSAECONNRESET = ECONNRESET;
constexpr int WSAECONNREFUSED = ECONNREFUSED;
constexpr int WSAENETUNREACH = ENETUNREACH;
constexpr int WSAEHOSTUNREACH = EHOSTUNREACH;
constexpr int WSAEINVAL = EINVAL;
constexpr int WSAEFAULT = EFAULT;
constexpr int WSAEACCES = EACCES;
constexpr int WSAEAFNOSUPPORT = EAFNOSUPPORT;
constexpr int WSAEMSGSIZE = EMSGSIZE;
constexpr int WSA_IO_PENDING = EINPROGRESS;

// Time & Process helpers
inline uint64_t GetTickCount64() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000 + (ts.tv_nsec / 1000000);
}

inline DWORD GetCurrentProcessId() {
    return static_cast<DWORD>(getpid());
}

inline DWORD GetCurrentThreadId() {
    return static_cast<DWORD>(pthread_self());
}

inline void OutputDebugStringA(const char*) {}

#ifndef sprintf_s
#define sprintf_s(buf, ...) snprintf(buf, sizeof(buf), __VA_ARGS__)
#endif

// WSA compatibility stubs for tests
struct WSADATA {
    WORD wVersion;
    WORD wHighVersion;
    char szDescription[257];
    char szSystemStatus[129];
    unsigned short iMaxSockets;
    unsigned short iMaxUdpDg;
    char* lpVendorInfo;
};

#ifndef MAKEWORD
#define MAKEWORD(a, b) ((WORD)(((BYTE)(((uintptr_t)(a)) & 0xff)) | ((WORD)((BYTE)(((uintptr_t)(b)) & 0xff)) << 8)))
#endif

inline int WSAStartup(WORD, WSADATA*) { return 0; }
inline int WSACleanup() { return 0; }

#endif // _WIN32
