#include "src/xenia/gpu/dxil/guest_texture_stage.h"
#include <rthreads/rthreads.h>
#include <features/features_cpu.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"native binding line %u\n",(unsigned)__LINE__); exit(1); } } while (0)
void *xe_binding_native_new(void);
void xe_binding_native_free(void *);
int xe_binding_native_publish(void *, const XeDxilTextureLayout *);
int xe_binding_native_check(void *, const XeDxilTextureLayout *, const void **, const void **);
static XeDxilTextureLayout layout;
static void read_bindings(void *owner)
{
   unsigned i = 0;
   int result;
   int64_t start = cpu_features_get_time_usec();
   while (i < 1000) {
      result = xe_binding_native_check(owner, &layout, NULL, NULL);
      CHECK(result == 0 || result == 1);
      if (result) ++i;
      else CHECK(cpu_features_get_time_usec() - start < 5000000);
   }
}
static void publish_bindings(void *owner)
{
   unsigned i;
   for(i=0;i<1000;++i) CHECK(xe_binding_native_publish(owner,&layout));
}
int main(void)
{
   void *owner;
   const void *textures, *samplers, *t, *s;
   sthread_t *threads[8];
   unsigned i;
   owner = xe_binding_native_new(); CHECK(owner);
   layout.texture_count = XE_DXIL_TEXTURE_BINDINGS_MAX;
   layout.sampler_count = XE_DXIL_SAMPLER_BINDINGS_MAX;
   layout.used_texture_mask = 0xFFFFFFFFu;
   for (i = 0; i < layout.texture_count; ++i) {
      layout.textures[i].fetch_constant = i % 32;
      layout.textures[i].dimension = i % 4;
      layout.textures[i].is_signed = i % 2;
   }
   for (i = 0; i < layout.sampler_count; ++i) {
      layout.samplers[i].fetch_constant = i % 32;
      layout.samplers[i].mag_filter = i % 4;
      layout.samplers[i].min_filter = (i / 4) % 4;
      layout.samplers[i].mip_filter = (i / 16) % 4;
      layout.samplers[i].aniso_filter = (i / 64) % 8;
   }
   CHECK(!xe_binding_native_check(owner, &layout, NULL, NULL));
   layout.texture_count = XE_DXIL_TEXTURE_BINDINGS_MAX + 1;
   CHECK(!xe_binding_native_publish(owner, &layout));
   CHECK(!xe_binding_native_check(owner, &layout, NULL, NULL));
   layout.texture_count = XE_DXIL_TEXTURE_BINDINGS_MAX;
   for (i = 0; i < 4; ++i) { threads[i] = sthread_create(read_bindings, owner); CHECK(threads[i]); }
   for (i = 4; i < 8; ++i) { threads[i] = sthread_create(publish_bindings, owner); CHECK(threads[i]); }
   for (i = 0; i < 8; ++i) sthread_join(threads[i]);
   CHECK(xe_binding_native_check(owner, &layout, &textures, &samplers) == 1);
   CHECK(textures && (const unsigned char *)samplers == (const unsigned char *)textures + layout.texture_count * 4);
   for (i = 0; i < 100; ++i) CHECK(xe_binding_native_publish(owner, &layout));
   CHECK(xe_binding_native_check(owner, &layout, &t, &s) == 1 && t == textures && s == samplers);
   layout.textures[0].is_signed ^= 1;
   CHECK(!xe_binding_native_publish(owner, &layout));
   layout.textures[0].is_signed ^= 1;
   layout.samplers[0].aniso_filter ^= 1;
   CHECK(!xe_binding_native_publish(owner, &layout));
   layout.samplers[0].aniso_filter ^= 1;
   layout.used_texture_mask ^= 1;
   CHECK(!xe_binding_native_publish(owner, &layout));
   layout.used_texture_mask ^= 1;
   layout.texture_count = XE_DXIL_TEXTURE_BINDINGS_MAX + 1;
   CHECK(!xe_binding_native_publish(owner, &layout));
   layout.texture_count = XE_DXIL_TEXTURE_BINDINGS_MAX;
   CHECK(xe_binding_native_check(owner, &layout, &t, &s) == 1 && t == textures && s == samplers);
   xe_binding_native_free(owner);
   memset(&layout, 0, sizeof(layout));
   owner = xe_binding_native_new(); CHECK(owner);
   CHECK(xe_binding_native_publish(owner, &layout));
   CHECK(xe_binding_native_check(owner, &layout, &t, &s) == 1 && !t && !s);
   xe_binding_native_free(owner);
   owner = xe_binding_native_new(); CHECK(owner);
   layout.sampler_count = 17;
   CHECK(xe_binding_native_publish(owner, &layout));
   CHECK(xe_binding_native_check(owner, &layout, &t, &s) == 1 && !t && s);
   xe_binding_native_free(owner);
   puts("PASS native packed bindings, contiguous ownership, concurrent publication, borrowed views and mismatch rejection");
   return 0;
}
