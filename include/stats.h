
// stats.h
#pragma once
#include <vector>

struct alignas(64) Stats {
    unsigned long long accepts = 0;
    unsigned long long recvOps = 0;
    unsigned long long sendOps = 0;
    unsigned long long bytesIn = 0;
    unsigned long long bytesOut = 0;
    unsigned long long closes = 0;
    unsigned long long errors = 0;
    char _pad[64 - (sizeof(unsigned long long) * 7) % 64]{};
};

extern std::vector<Stats> g_stats;
extern thread_local Stats* t_st;

Stats aggregate();
