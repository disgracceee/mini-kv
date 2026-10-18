#include "ctrlHandler.h"
#include "platform.h"


BOOL WINAPI ctrlHandler(DWORD type) {
    switch (type) {
    case CTRL_BREAK_EVENT:
    case CTRL_SHUTDOWN_EVENT:
    case CTRL_C_EVENT:
    case CTRL_CLOSE_EVENT:
        std::cout << "\n[main] shutdown requested\n";
        cfg::g_stop.store(true, std::memory_order_release);
        if (cfg::g_lst != INVALID_SOCKET) {
            closesocket(cfg::g_lst);
            cfg::g_lst = INVALID_SOCKET;
        }
        if (cfg::g_stopEvent)
            SetEvent(cfg::g_stopEvent);
        return TRUE;
    default:
        return FALSE;
    }
}