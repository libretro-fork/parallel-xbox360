/* Actual emitter coverage/counts, and unchanged-key binary comparisons. C89. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core_resolve_loads_emit.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %u: %s\n",(unsigned)__LINE__,#x); exit(1); } } while (0)
static unsigned calls, modules, compared;
static size_t binary_bytes;
#ifdef XE_TEST_RESOLVE_DXIL
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4201) /* Existing native DXIL ABI unions only. */
#endif
#include "edram_dump.h"
#include "dxil_internal.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif
const struct dxil_value *xe_test_resolve_call(struct dxil_module *m, const struct dxil_func *f,
    const struct dxil_value **args, size_t n) {
  if (!strncmp(f->name, "dx.op.textureLoad", 17)) ++calls;
  return dxil_emit_call(m, f, args, n);
}
#define EXTENSION "dxil"
#else
#define EXTENSION "spv"
#endif
typedef struct context {
  xe_test_resolve_options options;
  const char *output, *baseline;
  unsigned expected, optimized;
} context;
static void path_make(char *path, size_t capacity, const char *dir, const xe_test_resolve_options *o) {
  int size = snprintf(path, capacity, "%s/%u-%u-%u-%u-%u-%u." EXTENSION,
      dir, o->format, o->depth, o->msaa, o->scale, o->config, o->direct);
  CHECK(size > 0 && (size_t)size < capacity);
}
static int inspect(void *opaque, const uint32_t *words, size_t count) {
  context *c = (context *)opaque;
  char path[1024];
  FILE *file;
  size_t bytes = count * sizeof(uint32_t);
#ifndef XE_TEST_RESOLVE_DXIL
  size_t offset = 5;
  CHECK(count >= 5 && words[0] == 0x07230203);
  calls = 0;
  while (offset < count) {
    uint32_t length = words[offset] >> 16;
    CHECK(length && length <= count - offset);
    if ((words[offset] & 65535) == 95) ++calls; /* OpImageFetch. */
    offset += length;
  }
  CHECK(offset == count);
#else
  CHECK(count > 8 && words[0] == 0x43425844); /* DXBC container. */
#endif
  if (calls != c->expected) {
    fprintf(stderr, "%u/%u/%u scale %u config %u direct %u: %u loads, wanted %u\n",
        c->options.format, c->options.depth, c->options.msaa, c->options.scale,
        c->options.config, c->options.direct, calls, c->expected); return 0;
  }
  path_make(path, sizeof(path), c->output, &c->options);
  file = fopen(path, "wb"); CHECK(file);
  CHECK(fwrite(words, 1, bytes, file) == bytes); CHECK(!fclose(file));
  if (c->baseline && !c->optimized) {
    unsigned char scratch[4096];
    size_t consumed = 0;
    path_make(path, sizeof(path), c->baseline, &c->options);
    file = fopen(path, "rb"); CHECK(file);
    while (consumed < bytes) {
      size_t n = bytes - consumed;
      if (n > sizeof(scratch)) n = sizeof(scratch);
      CHECK(fread(scratch, 1, n, file) == n);
      CHECK(!memcmp(scratch, (const unsigned char *)words + consumed, n)); consumed += n;
    }
    CHECK(fgetc(file) == EOF && !ferror(file)); CHECK(!fclose(file)); ++compared;
  }
  ++modules; binary_bytes += bytes; return 1;
}
int main(int argc, char **argv) {
  static const unsigned formats[14] = {0,1,2,3,4,5,6,7,10,12,14,15,0,1};
  static const unsigned scales[7] = {1,2,3,4,8,16,32};
  unsigned f, s, msaa, config, direct;
  context c;
  CHECK(argc == 2 || argc == 3); memset(&c, 0, sizeof(c));
  c.output = argv[1]; c.baseline = argc == 3 ? argv[2] : NULL;
  for (f = 0; f < 14; ++f) for (msaa = 0; msaa < 3; ++msaa)
  for (s = 0; s < 7; ++s) for (config = 0; config < 3; ++config)
  for (direct = 0; direct < 2; ++direct) {
    unsigned run = f < 12 && (formats[f] == 5 || formats[f] == 7 || formats[f] == 15) ? 4 : 8;
#ifdef XE_TEST_RESOLVE_DXIL
    if (scales[s] > 16) continue; /* Native DXIL emitter rejects larger scales. */
#endif
    c.options.format = formats[f]; c.options.depth = f >= 12; c.options.msaa = msaa;
    c.options.scale = scales[s]; c.options.config = config; c.options.direct = direct;
    c.optimized = direct && config == 1 && scales[s] > 1 && scales[s] <= 16 && !(scales[s] & (scales[s] - 1));
    c.expected = direct ? run : 1;
#ifndef XE_RESOLVE_LOADS_BASELINE
    if (c.optimized) c.expected = scales[s] < run ? run / scales[s] : 1;
#endif
    if (f >= 12) c.expected *= 2; /* Depth plus separate stencil view. */
#ifdef XE_TEST_RESOLVE_DXIL
    {
      XeDxilEdramDumpOptions options;
      XeDxilShaderBinary binary;
      memset(&options, 0, sizeof(options)); memset(&binary, 0, sizeof(binary));
      options.format = formats[f]; options.depth = f >= 12; options.msaa = msaa;
      options.scale_x = scales[s]; options.scale_y = scales[s] == 1 ? 1 : 3;
      options.native_source = config != 0; options.native_layout = config == 2;
      options.direct_resolve = direct; options.msaa_2x_supported = config != 0;
      options.source_uint = !options.depth && ((options.format >= 4 && options.format <= 7) || options.format >= 14);
      options.round_depth = 1; calls = 0;
      CHECK(xe_dxil_build_edram_dump(&options, &binary)); CHECK(!(binary.size % 4));
      CHECK(inspect(&c, (const uint32_t *)binary.data, binary.size / 4));
      xe_dxil_shader_binary_free(&binary);
    }
#else
    CHECK(xe_test_resolve_spv(&c.options, inspect, &c));
#endif
  }
  printf("actual " EXTENSION " emitters: %u modules, %u unaffected byte-identical, %lu total bytes; load counts PASS\n",
      modules, compared, (unsigned long)binary_bytes);
  return 0;
}
