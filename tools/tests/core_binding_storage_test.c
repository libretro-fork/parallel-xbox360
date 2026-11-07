#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "binding line %u\n", (unsigned)__LINE__); exit(1); } } while (0)
static unsigned calls, frees, fail;
static void *test_calloc(size_t count, size_t bytes)
{
   ++calls;
   return fail ? NULL : calloc(count, bytes);
}
static void test_free(void *data)
{
   if (data) ++frees;
   free(data);
}
#define calloc test_calloc
#define free test_free
#include "libretro/core_binding_storage.c"
#undef calloc
#undef free
int main(void)
{
   xe_binding_storage storage = {0};
   void *before;
   size_t i;
   CHECK(!xe_binding_storage_allocate(NULL, 0, 0));
   CHECK(xe_binding_storage_allocate(&storage, 0, 0));
   CHECK(!calls && !storage.data);
   CHECK(!xe_binding_storage_allocate(&storage, (size_t)-1, 1));
   CHECK(!xe_binding_storage_allocate(&storage, (size_t)-1 / 4 + 1, 0));
   CHECK(!calls && !storage.texture_count);
   fail = 1;
   CHECK(!xe_binding_storage_allocate(&storage, 192, 2000));
   CHECK(calls == 1 && !storage.data && !storage.sampler_count);
   fail = 0;
   CHECK(xe_binding_storage_allocate(&storage, 192, 2000));
   CHECK(calls == 2 && storage.texture_count == 192 && storage.sampler_count == 2000);
   before = storage.data;
   for (i = 0; i < (192 + 2000) * sizeof(uint32_t); ++i)
      CHECK(!((unsigned char *)storage.data)[i]);
   memset(storage.data, 0xA5, (192 + 2000) * sizeof(uint32_t));
   CHECK(!xe_binding_storage_allocate(&storage, 1, 1));
   CHECK(storage.data == before && calls == 2 && !frees);
   xe_binding_storage_reset(&storage);
   CHECK(!storage.data && !storage.texture_count && !storage.sampler_count && frees == 1);
   xe_binding_storage_reset(&storage);
   xe_binding_storage_reset(NULL);
   CHECK(frees == 1);
   CHECK(xe_binding_storage_allocate(&storage, 0, 17));
   CHECK(storage.data && !storage.texture_count && storage.sampler_count == 17);
   xe_binding_storage_reset(&storage);
   CHECK(calls == 3 && frees == 2);
   puts("PASS exact binding storage, overflow, allocation failure, ownership, zeroing and reuse");
   return 0;
}
