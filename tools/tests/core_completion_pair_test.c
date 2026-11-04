/* Publication and recycling across two independently retiring participants. */
#include <stdio.h>
#include <string.h>
#include <rthreads/rthreads.h>
#include "core_completion_pair.h"
#define COUNT 8192
struct slot {
   xe_completion_pair pair;
   unsigned writes[2];
   unsigned retired;
};
static struct slot slots[COUNT];
static retro_atomic_int_t failed;
static unsigned generation;
static void release_slots(void *opaque) {
   unsigned role = *(const unsigned *)opaque;
   unsigned n, i;
   for (n = 0; n < COUNT; ++n) {
      i = role ? COUNT - 1 - n : n;
      slots[i].writes[role] = generation;
      if (xe_completion_pair_release(&slots[i].pair)) {
         if (slots[i].writes[0] != generation || slots[i].writes[1] != generation)
            retro_atomic_store_release_int(&failed, 1);
         ++slots[i].retired;
      }
   }
}
int main(void) {
   unsigned role[2] = {0, 1};
   unsigned i;
   sthread_t *a, *b;
   xe_completion_pair delayed;
   xe_completion_pair_init(&delayed);
   if (xe_completion_pair_release(&delayed) ||
         !xe_completion_pair_release(&delayed)) return 1;
   retro_atomic_int_init(&failed, 0);
   for (generation = 1; generation <= 32; ++generation) {
      memset(slots, 0, sizeof(slots));
      for (i = 0; i < COUNT; ++i) xe_completion_pair_init(&slots[i].pair);
      a = sthread_create(release_slots, &role[0]);
      b = sthread_create(release_slots, &role[1]);
      if (!a || !b) return 2;
      sthread_join(a); sthread_join(b);
      if (retro_atomic_load_acquire_int(&failed)) return 3;
      for (i = 0; i < COUNT; ++i)
         if (slots[i].retired != 1 || slots[i].writes[0] != generation ||
               slots[i].writes[1] != generation) return 4;
   }
   puts("PASS: 262144 paired retirements, opposite release order, publication and reuse");
   return 0;
}
