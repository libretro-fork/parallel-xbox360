#include "libretro/core_state_wait.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if (!(x)) abort(); } while (0)
int main(void)
{
   xe_state_wait wait = {0, 0};
   uint64_t deadline;
   CHECK(xe_state_relative_millis(-10000) == 1);
   CHECK(xe_state_relative_millis(-1) == 0);
   CHECK(xe_state_relative_millis(INT64_MIN) == (((uint64_t)214748 << 32) + 1566804069UL));
   CHECK(xe_state_wait_valid(&wait));
   CHECK(xe_state_wait_capture_delay(&wait, 100, 350));
   CHECK(wait.kind == XE_STATE_WAIT_DELAY && wait.remaining_ms == 250);
   CHECK(xe_state_wait_rebase(&wait, 5000, &deadline) && deadline == 5250);
   CHECK(xe_state_wait_capture_delay(&wait, 350, 100) && !wait.remaining_ms);
   CHECK(xe_state_wait_capture_delay(&wait, 0, UINT32_MAX));
   CHECK(!xe_state_wait_rebase(&wait, UINT64_MAX, &deadline));
   CHECK(!xe_state_wait_capture_delay(&wait, 0, (uint64_t)UINT32_MAX + 1));
   wait.kind = XE_STATE_WAIT_NONE;
   CHECK(!xe_state_wait_valid(&wait));
   wait.kind = 999;
   wait.remaining_ms = 0;
   CHECK(!xe_state_wait_valid(&wait));
   puts("PASS: restartable delay durations retain remaining time and reject invalid kinds/overflow");
   return 0;
}
