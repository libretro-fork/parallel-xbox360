/* Coalesced guest-to-frontend stop notification. MSVC C89-compatible. */
#include "core_events.h"
#ifdef _WIN32
#include <windows.h>
static volatile LONG title_stop;
static int exchange_stop(int value) {
  return (int)InterlockedExchange(&title_stop, (LONG)value);
}
#else
static int title_stop;
static int exchange_stop(int value) {
  return __atomic_exchange_n(&title_stop, value, __ATOMIC_SEQ_CST);
}
#endif
void xe_libretro_request_title_stop(void) { (void)exchange_stop(1); }
int xe_libretro_take_title_stop(void) { return exchange_stop(0); }
void xe_libretro_clear_title_stop(void) { (void)exchange_stop(0); }
