
#pragma once
#include "platform.h"
#include <atomic>
#include <mutex>
#include <unordered_set>

struct Conn;
namespace cfg {
    inline constexpr bool VERBOSE = false;
    inline constexpr int   PORT = 5555;
    inline constexpr int   BACKLOG_POSTED = 64;
    inline constexpr int   RECV_BUF = 4096;
    inline constexpr DWORD LOCALADDRLEN = sizeof(sockaddr_storage) + 16;
    inline constexpr DWORD REMOTEADDRLEN = sizeof(sockaddr_storage) + 16;
    inline constexpr DWORD ACCEPTBUFF = LOCALADDRLEN + REMOTEADDRLEN;

    inline std::unordered_set<Conn*> g_conns;

    inline HANDLE g_iocp = nullptr;
    inline SOCKET g_lst = INVALID_SOCKET;
    inline HANDLE g_stopEvent = NULL;

    inline std::atomic<bool> g_stop{ false };
    inline std::atomic<long> g_liveConns{ 0 };
    inline std::atomic<long> g_peakConns{ 0 };
    inline std::atomic<long> g_nextId{ 0 };
    inline std::atomic<long> g_acceptsInFlight{ 0 };
    inline std::mutex        g_connsMx;

    inline unsigned long long nowMs() { return GetTickCount64(); }
}
