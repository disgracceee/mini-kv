#include "platform.h"





inline constexpr unsigned long IDLE_TIMEOUT_MS = 60'000;
inline constexpr unsigned long DRAIN_TIMEOUT_MS = 5'000;


void worker(unsigned idx);
void reaper();
void monitor();
