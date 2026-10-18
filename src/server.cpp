#include "io.h"
#include "protocol.h"
#include "server.h"

void worker(unsigned idx) {
    t_st = &g_stats[idx];
    for (;;) {
        DWORD bytes = 0;
        ULONG_PTR key = NULL;
        LPOVERLAPPED lp = NULL;

        BOOL ok = GetQueuedCompletionStatus(cfg::g_iocp, &bytes, &key, &lp, INFINITE);

        if (!lp) {
            break;
        }

        IoCtx* io = CONTAINING_RECORD(lp, IoCtx, ov);

        if (io->op == Op::Accept) {
            AcceptCtx* ac = static_cast<AcceptCtx*>(io);
            if (!ok) {
                cfg::g_acceptsInFlight.fetch_sub(1);
                if (ac->s != INVALID_SOCKET) {
                    closesocket(ac->s);
                    ac->s = INVALID_SOCKET;
                }
                if (cfg::g_stop.load(std::memory_order_acquire)) {
                    delete ac;
                    if (cfg::g_stopEvent)
                        SetEvent(cfg::g_stopEvent);
                    continue;
                }
                if (!postAccept(ac)) {
                    delete ac;
                }
                continue;
            }
            onAccept(ac);
            continue;
        }

        Conn* c = reinterpret_cast<Conn*>(key);

        if (!ok) {
            closeConn(c);
            release(c);
            continue;
        }
        else if (io->op == Op::Recv) {
            ++t_st->recvOps;
            t_st->bytesIn += bytes;
            if (bytes == 0) {
                closeConn(c);
                release(c);
                continue;
            }
            else {
                c->touch();
                if (!on_data(c, c->buff, bytes)) {
                    std::cerr << std::format("[conn {}] protocol error -> close\n", c->id);
                    closeConn(c);
                    release(c);
                    continue;
                }
                if (!startSend(c)) {
                    closeConn(c);
                    release(c);
                    continue;
                }
            }
        }
        else if (io->op == Op::Send) {
            ++t_st->sendOps;
            t_st->bytesOut += bytes;
            if (bytes == 0) {
                closeConn(c);
                release(c);
                continue;
            }
            else {
                c->sendsent += bytes;
                if (c->sendsent < c->sendTotal) {
                    c->touch();
                    if (!postSend(c)) {
                        closeConn(c);
                        release(c);
                        continue;
                    }
                }
                else {
                    c->outbuf.clear();
                    c->sendTotal = c->sendsent = 0;
                    c->touch();
                    if (!postRecv(c)) {
                        closeConn(c);
                        release(c);
                        continue;
                    }
                }
            }
        }
        release(c);
    }
}

void reaper() {

    if (IDLE_TIMEOUT_MS == 0) {
        return;
    }

    for (;;) {

        const DWORD w = WaitForSingleObject(cfg::g_stopEvent, 1000);
        if (w != WAIT_TIMEOUT)
            return;

        const long long now = cfg::nowMs();
        std::vector<Conn*> dead;
        {
            std::lock_guard<std::mutex> lk(cfg::g_connsMx);
            for (Conn* c : cfg::g_conns) {
                const long long last = c->lastAct.load(std::memory_order_relaxed);
                if (now - last >= IDLE_TIMEOUT_MS) {
                    addRef(c);
                    dead.push_back(c);
                }
            }
        }
        for (Conn* c : dead) {
            std::cerr << std::format("[conn {}] idle timeout -> close\n", c->id);
            closeConn(c);
            release(c);
        }
    }
}
void monitor() {
    Stats prev = aggregate();
    auto prevt = std::chrono::steady_clock::now();

    for (;;) {
        const DWORD n = WaitForSingleObject(cfg::g_stopEvent, 2000);

        if (n != WAIT_TIMEOUT) {
            return;
        }

        Stats post = aggregate();
        auto nowt = std::chrono::steady_clock::now();
        double dt = std::chrono::duration<double>(nowt - prevt).count();

        if (dt <= 0.0) {
            continue;
        }

        double bytesIn = ((post.bytesIn - prev.bytesIn) / dt) / (1024.0 * 1024.0);
        double bytesOut = ((post.bytesOut - prev.bytesOut) / dt) / (1024.0 * 1024.0);
        double acc = (post.accepts - prev.accepts) / dt;

        std::cout << std::format("[stat] live = {}, peak = {}, acc/s = {}, MB/s in = {}, MB/s out "
            "= {}, recv = {}, send = {}, closes = {}, errors = {}\n",
            cfg::g_liveConns.load(), cfg::g_peakConns.load(), acc, bytesIn, bytesOut,
            post.recvOps, post.sendOps, post.closes, post.errors);

        prev = post;
        prevt = nowt;
    }
}

