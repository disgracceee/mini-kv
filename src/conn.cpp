#include "conn.h"
#include <format>
#include <iostream>

Conn::Conn() {
    id = cfg::g_nextId.fetch_add(1) + 1;
    long live = cfg::g_liveConns.fetch_add(1) + 1;
    long peak = cfg::g_peakConns.load();
    while (live > peak && !cfg::g_peakConns.compare_exchange_weak(peak, live)) {
    }
    lastAct.store(cfg::nowMs(), std::memory_order_relaxed);
}
Conn::~Conn() {
    if (s != INVALID_SOCKET) {
        closesocket(s);
        s = INVALID_SOCKET;
    }
    cfg::g_liveConns.fetch_sub(1);
}

void addRef(Conn* c) {
    c->ref.fetch_add(1, std::memory_order_relaxed);
}
void release(Conn* c) {
    if (c->ref.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        const long id = c->id;
        delete c;
        std::cerr << std::format("[conn {}] freed (live = {})\n", id, cfg::g_liveConns.load());
        if (cfg::g_stopEvent)
            SetEvent(cfg::g_stopEvent);
    }
}
void Conn::touch() { lastAct.store(cfg::nowMs(), std::memory_order_relaxed); }