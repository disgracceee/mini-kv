#include <stats.h>





std::vector<Stats> g_stats;
thread_local Stats* t_st = nullptr;

Stats aggregate() {
    Stats t;
    for (const Stats& s : g_stats) {
        t.accepts += s.accepts;
        t.recvOps += s.recvOps;
        t.sendOps += s.sendOps;
        t.bytesIn += s.bytesIn;
        t.bytesOut += s.bytesOut;
        t.closes += s.closes;
        t.errors += s.errors;
    }
    return t;
}

