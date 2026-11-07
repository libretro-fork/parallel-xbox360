#include "core_binding_storage.h"
#include <stdint.h>
#include <stdlib.h>

int xe_binding_storage_allocate(xe_binding_storage *storage,
      size_t textures, size_t samplers)
{
   void *data;
   size_t count;
   if (!storage || storage->data || storage->texture_count || storage->sampler_count)
      return 0;
   if (textures > (size_t)-1 - samplers)
      return 0;
   count = textures + samplers;
   if (count > (size_t)-1 / sizeof(uint32_t))
      return 0;
   data = count ? calloc(count, sizeof(uint32_t)) : NULL;
   if (count && !data)
      return 0;
   storage->data = data;
   storage->texture_count = textures;
   storage->sampler_count = samplers;
   return 1;
}
void xe_binding_storage_reset(xe_binding_storage *storage)
{
   if (!storage) return;
   free(storage->data);
   storage->data = NULL;
   storage->texture_count = storage->sampler_count = 0;
}
