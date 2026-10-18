#pragma once
#include "config.h"

inline constexpr uint32_t MAX_PAYLOAD = 1 * 1024 * 1024;
inline constexpr size_t MAX_INBUF = 4 + MAX_PAYLOAD + 64 * 1024;

void enqueueFrame(Conn* c, const char* data, uint32_t len);
void handle_message(Conn* c, const char* str, std::size_t len);
bool on_data(Conn* c, const char* data, std::size_t size);
