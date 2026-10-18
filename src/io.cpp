#include "io.h"
#include "registry.h"

bool postRecv(Conn* c) {
    if (c->closing.load(std::memory_order_acquire))
        return false;

    ZeroMemory(&c->ioRecv.ov, sizeof(c->ioRecv.ov));

    c->wb.buf = c->buff;
    c->wb.len = sizeof(c->buff);
    c->flag = 0;

    addRef(c);

    if (WSARecv(c->s, &c->wb, 1, NULL, &c->flag, &c->ioRecv.ov, NULL) == SOCKET_ERROR) {
        int e = WSAGetLastError();
        if (e != WSA_IO_PENDING) {
            if (e != WSAECONNRESET && e != WSAECONNABORTED && e != WSAENOTSOCK && e != WSAEINTR) {
                if (t_st)
                    ++t_st->errors;
                std::cerr << std::format("Error WSARecv: {}\n", e);
            }
            release(c);
            return false;
        }
    }
    return true;
}
bool postSend(Conn* c) {
    if (c->closing.load(std::memory_order_acquire))
        return false;

    ZeroMemory(&c->ioSend.ov, sizeof(c->ioSend.ov));
    c->wb.buf = c->outbuf.data() + c->sendsent;
    c->wb.len = c->sendTotal - c->sendsent;

    c->flag = 0;

    addRef(c);

    if (WSASend(c->s, &c->wb, 1, NULL, c->flag, &c->ioSend.ov, NULL) == SOCKET_ERROR) {
        int e = WSAGetLastError();
        if (e != WSA_IO_PENDING) {
            if (e != WSAECONNRESET && e != WSAECONNABORTED && e != WSAENOTSOCK && e != WSAEINTR) {
                if (t_st)
                    ++t_st->errors;
                std::cerr << std::format("Error WSASend: {}\n", e);
            }
            release(c);
            return false;
        }
    }
    return true;
}

bool startSend(Conn* c) {
    c->sendTotal = static_cast<DWORD>(c->outbuf.size());
    c->sendsent = 0;
    if (c->sendTotal == 0)
        return postRecv(c);

    return postSend(c);
}

bool loadExtensions(SOCKET s) {
    GUID gAccept = WSAID_ACCEPTEX;
    GUID gAddrs = WSAID_GETACCEPTEXSOCKADDRS;

    DWORD bytes = 0;

    if (WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER, &gAccept, sizeof(gAccept), &g_AcceptEx,
        sizeof(g_AcceptEx), &bytes, NULL, NULL) == SOCKET_ERROR) {
        std::cerr << std::format("Error EXTENSION(AcceptEx): {}\n", WSAGetLastError());
        return false;
    }
    if (WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER, &gAddrs, sizeof(gAddrs),
        &g_GetAcceptExSockAddrs, sizeof(g_GetAcceptExSockAddrs), &bytes, NULL,
        NULL) == SOCKET_ERROR) {
        std::cerr << std::format("Error EXTENSION(g_GetAcceptExSockAddrs): {}\n", WSAGetLastError());
        return false;
    }

    return true;
}

bool postAccept(AcceptCtx* ac) {
    if (cfg::g_stop.load(std::memory_order_acquire))
        return false;
    ZeroMemory(&ac->ov, sizeof(ac->ov));

    ac->s = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
    if (ac->s == SOCKET_ERROR) {
        std::cerr << "Error WSASocket: " << WSAGetLastError() << '\n';
        return false;
    }

    cfg::g_acceptsInFlight.fetch_add(1);

    DWORD bytes = 0;
    BOOL ok = g_AcceptEx(cfg::g_lst, ac->s, ac->buf, 0, cfg::LOCALADDRLEN, cfg::REMOTEADDRLEN, &bytes, &ac->ov);

    if (!ok) {
        int e = WSAGetLastError();
        if (e != WSA_IO_PENDING) {
            cfg::g_acceptsInFlight.fetch_sub(1);
            if (!cfg::g_stop.load())
                std::cerr << std::format("Error AcceptEx: {}\n", e);
            closesocket(ac->s);
            ac->s = INVALID_SOCKET;
            return false;
        }
    }
    return true;
}


void onAccept(AcceptCtx* ac) {
    if (cfg::g_stop.load(std::memory_order_acquire))
        return;
    cfg::g_acceptsInFlight.fetch_sub(1);

    SOCKET sock = ac->s;
    ac->s = INVALID_SOCKET;

    if (setsockopt(sock, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
        reinterpret_cast<const char*>(&cfg::g_lst), sizeof(cfg::g_lst)) == SOCKET_ERROR) {
        std::cerr << std::format("Error SO_UPDATE_ACCEPT_CONTEXT: {}\n", WSAGetLastError());
        closesocket(sock);
        if (!postAccept(ac))
            delete ac;
        return;
    }

    BOOL nodelay = TRUE;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&nodelay),
        sizeof(nodelay));

    if constexpr (cfg::VERBOSE) {
        sockaddr* local = nullptr;
        sockaddr* remote = nullptr;

        int locallen = 0;
        int remotelen = 0;

        g_GetAcceptExSockAddrs(ac->buf, 0, cfg::LOCALADDRLEN, cfg::REMOTEADDRLEN, &local, &locallen, &remote,
            &remotelen);
        char ipbuf[INET6_ADDRSTRLEN] = "?";
        unsigned int port;
        if (remote && remote->sa_family == AF_INET) {
            sockaddr_in* r = reinterpret_cast<sockaddr_in*>(remote);
            inet_ntop(AF_INET, &r->sin_addr, ipbuf, sizeof(ipbuf));
            port = ntohs(r->sin_port);
        }
        std::cout << std::format("[conn] connected {}:{}. live conns: {}\n", ipbuf, port,
            cfg::g_liveConns.load());
    }

    Conn* c = new Conn;

    c->s = sock;
    sock = INVALID_SOCKET;

    if (CreateIoCompletionPort(reinterpret_cast<HANDLE>(c->s), cfg::g_iocp,
        reinterpret_cast<ULONG_PTR>(c), 0) == nullptr) {
        std::cerr << std::format("Error CreateIoCompletionPort: {} \n", WSAGetLastError());
        release(c);
        if (!postAccept(ac))
            delete ac;
        return;
    }
    registryInsert(c);

    if (!postRecv(c))
        closeConn(c);

    if (!postAccept(ac))
        delete ac;
}




