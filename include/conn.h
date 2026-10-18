#pragma once
#include "platform.h"
#include "config.h"
#include <atomic>
#include "vector"

enum class Op { Accept, Recv, Send };

struct IoCtx {
    OVERLAPPED ov;
    Op op;

    IoCtx(Op _op) : ov{}, op(_op) {}
};

struct AcceptCtx : IoCtx {
    SOCKET s = INVALID_SOCKET;
    char buf[cfg::ACCEPTBUFF]{};

    AcceptCtx() : IoCtx(Op::Accept) {}
};

struct Conn {
    IoCtx ioRecv{ Op::Recv };
    IoCtx ioSend{ Op::Send };
    SOCKET s = INVALID_SOCKET;
    char buff[cfg::RECV_BUF];
    WSABUF wb{};
    DWORD sendTotal = 0;
    DWORD sendsent = 0;
    DWORD flag = 0;

    std::vector<char> buffpreflength{};
    std::vector<char> outbuf{};

    long id = 0;
    std::atomic<long> ref{ 1 };
    std::atomic<bool> closing{ false };
    std::atomic<unsigned long long> lastAct{ 0 };

    Conn();
    ~Conn();
    Conn(const Conn&) = delete;
    Conn operator=(const Conn&) = delete;

    void touch();
};

void addRef(Conn* c);
void release(Conn* c);

