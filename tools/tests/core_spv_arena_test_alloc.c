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
static size_t pages, page_calls, page_fail = (size_t)-1;
void *xe_spv_test_page_alloc(size_t size)
{
   void *value;
   if (page_calls == page_fail) return NULL;
   value = malloc(size);
   if (value) { ++pages; ++page_calls; }
   return value;
}
void xe_spv_test_page_free(void *value)
{
   if (value) --pages;
   free(value);
}
size_t xe_spv_test_pages(void) { return pages; }
size_t xe_spv_test_page_calls(void) { return page_calls; }
void xe_spv_test_page_fail_after(size_t successful_calls)
{
   page_fail = successful_calls;
}
static int binary_fail_next;
void *xe_spv_test_binary_realloc(void *value, size_t size)
{
   if (binary_fail_next) { binary_fail_next = 0; return NULL; }
   return realloc(value, size);
}
void xe_spv_test_binary_fail_next(void) { binary_fail_next = 1; }

static int index_fail_next;
static size_t index_calls;
void *xe_spv_test_index_realloc(void *value, size_t size)
{
   ++index_calls;
   if (index_fail_next) { index_fail_next = 0; return NULL; }
   return realloc(value, size);
}
void xe_spv_test_index_fail_next(void) { index_fail_next = 1; }
size_t xe_spv_test_index_calls(void) { return index_calls; }
