#include "registry.h"

void registryInsert(Conn* c) {
    std::lock_guard lk(cfg::g_connsMx);
    cfg::g_conns.insert(c);
}

void closeConn(Conn* c) {
    if (c->closing.exchange(true))
        return;
    {
        std::lock_guard lk(cfg::g_connsMx);
        cfg::g_conns.erase(c);
    }
    if (t_st)
        ++t_st->closes;

    shutdown(c->s, SD_BOTH);
    CancelIoEx(reinterpret_cast<HANDLE>(c->s), nullptr);

    release(c);
}