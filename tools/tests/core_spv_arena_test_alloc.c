#include "core_spv_arena_test_alloc.h"
#include <stdlib.h>
static size_t live;
static int fail_next;
void *xe_spv_test_alloc(size_t size)
{
   void *value;
   if (fail_next) { fail_next = 0; return NULL; }
   value = malloc(size);
   if (value) ++live;
   return value;
}
void xe_spv_test_free(void *value)
{
   if (value) --live;
   free(value);
}
size_t xe_spv_test_live(void) { return live; }
void xe_spv_test_fail_next(void) { fail_next = 1; }
