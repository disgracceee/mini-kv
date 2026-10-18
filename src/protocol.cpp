
#include "protocol.h"
#include "Conn.h"
#include "store.h"



void enqueueFrame(Conn* c, const char* data, uint32_t len) {
    uint32_t be = htonl(len);
    const char* p = reinterpret_cast<const char*>(&be);
    c->outbuf.insert(c->outbuf.end(), p, p + 4);
    c->outbuf.insert(c->outbuf.end(), data, data + len);
}

void handle_message(Conn* c, const char* str, std::size_t len) {
    std::string_view msg(str, len);

    // --- режем на токены по пробелам ---
    std::string_view tok[3];
    int ntok = 0;
    std::size_t i = 0;

    while (i < msg.size() && ntok < 3) {
        while (i < msg.size() && msg[i] == ' ')
            ++i;

        if (i >= msg.size())
            break;

        std::size_t start = i;

        if (ntok == 2) {
            // последний токен — всё до конца
            // (значение может содержать пробелы)
            i = msg.size();
        }
        else {
            while (i < msg.size() && msg[i] != ' ')
                ++i;
        }

        tok[ntok++] = msg.substr(start, i - start);
    }

    auto reply = [&](std::string_view s) {
        enqueueFrame(c, s.data(), static_cast<uint32_t>(s.size()));
        };

    if (ntok == 0) {
        reply("ERR empty");
        return;
    }

    std::string_view cmd = tok[0];

    if (cmd == "SET") {
        if (ntok < 3) {
            reply("ERR usage: SET key value");
            return;
        }
        g_store.set(std::string(tok[1]), std::string(tok[2]));
        reply("OK");
    }
    else if (cmd == "GET") {
        if (ntok < 2) {
            reply("ERR usage: GET key");
            return;
        }
        std::string val;
        if (g_store.get(std::string(tok[1]), val))
            reply(val);
        else
            reply("NOT_FOUND");
    }
    else if (cmd == "DEL") {
        if (ntok < 2) {
            reply("ERR usage: DEL key");
            return;
        }
        if (g_store.del(std::string(tok[1])))
            reply("OK");
        else
            reply("NOT_FOUND");
    }
    else if (cmd == "PING") {
        reply("PONG");
    }
    else if (cmd == "DBSIZE") {
        reply(std::format("{}", g_store.size()));
    }
    else {
        reply("ERR unknown command");
    }

    std::cerr << std::format("[conn {}] cmd={} -> {} bytes in\n", c->id, cmd, len);
}

bool on_data(Conn* c, const char* data, std::size_t size) {
    if (c->buffpreflength.size() + size > MAX_INBUF)
        return false;

    c->buffpreflength.insert(c->buffpreflength.end(), data, data + size);

    std::size_t offset = 0;
    for (;;) {
        std::size_t available = c->buffpreflength.size() - offset;
        if (available < 4)
            break;

        uint32_t len_be = 0;
        std::memcpy(&len_be, c->buffpreflength.data() + offset, 4);
        uint32_t len = ntohl(len_be);

        if (len > MAX_PAYLOAD)
            return false;
        if (available < 4 + static_cast<std::size_t>(len))
            break;

        handle_message(c, c->buffpreflength.data() + offset + 4, len);

        offset += 4 + len;
    }

    if (offset) {
        c->buffpreflength.erase(c->buffpreflength.begin(), c->buffpreflength.begin() + offset);
    }
    return true;
}