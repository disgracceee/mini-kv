#include "platform.h"
#include "config.h"
#include "ctrlHandler.h"
#include "io.h"
#include "server.h"

int main() {
    WSAData wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::cerr << std::format("Error intialization WINSOCK: {}\n", WSAGetLastError());
        return 1;
    }
    cfg::g_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, 0);
    if (!cfg::g_iocp) {
        std::cerr << std::format("Error CreateCompletionPort: {}\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }
    SetConsoleCtrlHandler(ctrlHandler, TRUE);

    cfg::g_stopEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!cfg::g_stopEvent) {
        CloseHandle(cfg::g_iocp);
        std::cerr << std::format("Error CreateEventW: {}\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }
    cfg::g_lst = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
    if (cfg::g_lst == SOCKET_ERROR) {
        CloseHandle(cfg::g_iocp);
        CloseHandle(cfg::g_stopEvent);
        WSACleanup();
        std::cerr << std::format("Error WSASocket(g_lst): {}", WSAGetLastError());
        return 1;
    }

    BOOL reuse = TRUE;

    setsockopt(cfg::g_lst, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse),
        sizeof(reuse));

    sockaddr_in addr{};

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(cfg::PORT);

    if (bind(cfg::g_lst, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        std::cerr << std::format("Error bind: {}\n", WSAGetLastError());
        CloseHandle(cfg::g_iocp);
        CloseHandle(cfg::g_stopEvent);
        closesocket(cfg::g_lst);
        WSACleanup();
        return 1;
    }

    if (listen(cfg::g_lst, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << std::format("Error listen: {}\n", WSAGetLastError());
        CloseHandle(cfg::g_iocp);
        CloseHandle(cfg::g_stopEvent);
        closesocket(cfg::g_lst);
        WSACleanup();
        return 1;
    }

    if (!loadExtensions(cfg::g_lst)) {
        std::cerr << std::format("Error loadExtensions: {}\n", WSAGetLastError());
        CloseHandle(cfg::g_iocp);
        CloseHandle(cfg::g_stopEvent);
        closesocket(cfg::g_lst);
        WSACleanup();
        return 1;
    }

    if (CreateIoCompletionPort(reinterpret_cast<HANDLE>(cfg::g_lst), cfg::g_iocp, NULL, 0) == NULL) {
        std::cerr << std::format("Error bind listener to IOCP: {}\n", GetLastError());
        CloseHandle(cfg::g_iocp);
        CloseHandle(cfg::g_stopEvent);
        closesocket(cfg::g_lst);
        WSACleanup();
        return 1;
    }

    unsigned hw = std::thread::hardware_concurrency(); // Получаем от ОС какое количество потоков
    // можно запустить

    if (hw == 0) { // Если ОС сказала 0, то ставим = 2
        hw = 2;
    }
    const unsigned nWorkers = hw * 2;
    g_stats.assign(nWorkers,
        Stats{}); // Делаем размер g_stats = nWorkers и инициализурем Stats нулями.

    std::vector<std::thread> workers; // Создаем вектор потоков workers
    workers.reserve(nWorkers); // делаем размер нашего вектора равным количество потоков nWorkers

    for (unsigned i = 0; i < nWorkers; i++) {
        workers.emplace_back(worker, i);
    }

    std::thread monThread(monitor); // Еще 1 поток для monitor
    std::thread reapThread(reaper); // Еще 1 поток для reaper

    int posted = 0;

    for (int i = 0; i < cfg::BACKLOG_POSTED; i++) {
        AcceptCtx* ac = new AcceptCtx;
        if (!postAccept(ac)) {
            delete ac; // Наши BACKLOG_POSTED заготовки
            continue;
        }
        posted++;
    }
    if (posted == 0) {
        std::cerr << "Error: no AcceptEx posted\n";
        cfg::g_stop.store(true);
        SetEvent(cfg::g_stopEvent);
    }

    std::cout << std::format("[main] listenin on port = {} , workers = {}, accepts posted = {}\n",
        cfg::PORT, nWorkers, posted);
    std::cout << "Press CTRL+C to shutdown.\n";

    while (!cfg::g_stop.load(std::memory_order_acquire)) { // ЖДЕМ СИГНАЛ ОСТАНОВКИ
        WaitForSingleObject(cfg::g_stopEvent, INFINITE);
    }

    /* --- graceful shutdown --- */
    std::cout << "[main] closing live connections...\n";

    {
        std::vector<Conn*> all;
        {
            std::lock_guard<std::mutex> lk(cfg::g_connsMx);
            all.reserve(cfg::g_conns.size());
            for (Conn* c : cfg::g_conns) {
                addRef(c);
                all.push_back(c);
            }
        }
        for (Conn* c : all) {
            closeConn(c);
            release(c);
        }
    }

    const unsigned long long t0 = cfg::nowMs();
    while ((cfg::g_liveConns.load() > 0 || cfg::g_acceptsInFlight.load() > 0) &&
        cfg::nowMs() - t0 < DRAIN_TIMEOUT_MS) {
        WaitForSingleObject(cfg::g_stopEvent, 100);
    }
    if (cfg::g_liveConns.load() > 0 || cfg::g_acceptsInFlight.load() > 0) {
        std::cout << std::format("[main] drain timeout: live={} accepts={}\n", cfg::g_liveConns.load(),
            cfg::g_acceptsInFlight.load());
    }

    for (unsigned i = 0; i < nWorkers; ++i)
        PostQueuedCompletionStatus(cfg::g_iocp, 0, 0, NULL);
    for (std::thread& t : workers)
        if (t.joinable())
            t.join();

    SetEvent(cfg::g_stopEvent);

    if (monThread.joinable())
        monThread.join();

    if (reapThread.joinable())
        reapThread.join();

    Stats fin = aggregate();
    std::cout << std::format(
        "\n[main] totals: accepts={} recv={} send={} in={} B out={} B closes={} err={} peak={}\n",
        fin.accepts, fin.recvOps, fin.sendOps, fin.bytesIn, fin.bytesOut, fin.closes, fin.errors,
        cfg::g_peakConns.load());

    if (cfg::g_lst != INVALID_SOCKET)
        closesocket(cfg::g_lst);

    if (cfg::g_iocp)
        CloseHandle(cfg::g_iocp);

    if (cfg::g_stopEvent)
        CloseHandle(cfg::g_stopEvent);

    WSACleanup();

    std::cout << "[main] bye\n";
    return 0;
}
