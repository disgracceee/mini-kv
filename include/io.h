#pragma once
#include "registry.h"
#include "stats.h"
#include "conn.h"

inline LPFN_ACCEPTEX g_AcceptEx = NULL;
inline LPFN_GETACCEPTEXSOCKADDRS g_GetAcceptExSockAddrs = NULL;


bool postRecv(Conn* c);
bool postSend(Conn* c);
bool startSend(Conn* c);
bool loadExtensions(SOCKET s);
bool postAccept(AcceptCtx* ac);
void onAccept(AcceptCtx* ac);
