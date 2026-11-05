#include "core_gpu_worklist.h"
#include <stdlib.h>
#include <string.h>
int xe_gpu_worklist_init(xe_gpu_worklist *list, size_t capacity, size_t stride)
{
   size_t bytes, offset;
   volatile unsigned char *pages;
   if (!list || !capacity || !stride || capacity > (size_t)-1 / stride) return 0;
   if (list->data) return list->capacity == capacity && list->stride == stride;
   bytes = capacity * stride;
   list->data = malloc(bytes);
   if (!list->data) return 0;
   memset(list->data, 0, bytes);
   /* memset may fold into lazy calloc. Explicit stores touch every page on
    * supported hosts (whose pages are at least 4 KiB), including the last
    * partial page when malloc's address isn't page-aligned. */
   pages = (volatile unsigned char *)list->data;
   offset = 0;
   for (;;) {
      pages[offset] = 0;
      if (bytes - offset <= 4096) break;
      offset += 4096;
   }
   pages[bytes-1] = 0;
   list->capacity = capacity;
   list->stride = stride;
   list->count = 0;
   list->failed = 0;
   return 1;
}
void xe_gpu_worklist_reset(xe_gpu_worklist *list)
{
   if (!list) return;
   list->count = 0;
   list->failed = 0;
}
void *xe_gpu_worklist_append(xe_gpu_worklist *list)
{
   void *slot;
   if (!list) return NULL;
   if (!list->data || list->count >= list->capacity) {
      list->failed = 1;
      return NULL;
   }
   slot = (unsigned char *)list->data + list->count * list->stride;
   ++list->count;
   return slot;
}
void xe_gpu_worklist_close(xe_gpu_worklist *list)
{
   if (!list) return;
   free(list->data);
   list->data = NULL;
   list->count = list->capacity = list->stride = 0;
   list->failed = 0;
}
