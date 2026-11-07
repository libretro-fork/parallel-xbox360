/* Actual emitter coverage/counts, and unchanged-key binary comparisons. C89. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core_resolve_loads_emit.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %u: %s\n",(unsigned)__LINE__,#x); exit(1); } } while (0)
static unsigned calls, modules, compared, reduced, enlarged, origin_checks;
static unsigned wide_multiplies, high_extracts;
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
static const struct dxil_value *wide_product;
static int constant_value(struct dxil_module *m, const struct dxil_value *value, uint32_t *out) {
  struct dxil_const *c;
  LIST_FOR_EACH_ENTRY(struct dxil_const, c, &m->const_list, head) {
    if (&c->value == value && !c->undef) { *out = (uint32_t)c->int_value; return 1; }
  }
  return 0;
}
const struct dxil_value *xe_test_resolve_call(struct dxil_module *m, const struct dxil_func *f,
    const struct dxil_value **args, size_t n) {
  uint32_t opcode;
  const struct dxil_value *result;
  if (!strncmp(f->name, "dx.op.textureLoad", 17)) ++calls;
  result = dxil_emit_call(m, f, args, n);
  if (!strcmp(f->name, "dx.op.binaryWithTwoOuts.i32")) {
    CHECK(n == 3 && constant_value(m, args[0], &opcode) && opcode == 42);
    ++wide_multiplies; wide_product = result;
  }
  return result;
}
const struct dxil_value *xe_test_resolve_binop(struct dxil_module *m, enum dxil_bin_opcode op,
    const struct dxil_value *a, const struct dxil_value *b, enum dxil_opt_flags flags) {
#ifndef XE_RESOLVE_LOADS_BASELINE
  uint32_t divisor;
  if (op == DXIL_BINOP_UDIV || op == DXIL_BINOP_UREM)
    CHECK(constant_value(m, b, &divisor) && divisor);
#endif
  return dxil_emit_binop(m, op, a, b, flags);
}
const struct dxil_value *xe_test_resolve_extract(struct dxil_module *m,
    const struct dxil_value *value, unsigned index) {
  if (value == wide_product) { CHECK(index == 0); ++high_extracts; }
  return dxil_emit_extractval(m, value, index);
}
#define EXTENSION "dxil"
#else
#define EXTENSION "spv"
#endif
typedef struct context {
  xe_test_resolve_options options;
  const char *output, *baseline;
  unsigned expected, optimized, mode;
} context;
static void path_make(char *path, size_t capacity, const char *dir, const xe_test_resolve_options *o) {
  int size = snprintf(path, capacity, "%s/%u-%u-%u-%u-%u-%u-%u." EXTENSION,
      dir, o->format, o->depth, o->msaa, o->scale, o->config, o->direct, o->volume);
  CHECK(size > 0 && (size_t)size < capacity);
}
#ifndef XE_TEST_RESOLVE_DXIL
static uint32_t origin_value(const uint32_t *words, const uint32_t *definitions,
    uint32_t id, uint32_t root, uint32_t raw, uint32_t load, uint32_t base, unsigned depth) {
  const uint32_t *w;
  uint32_t a,b;
  CHECK(depth<8);
  if(id==root) return raw;
  if(id==load) return base;
  CHECK(id<65536 && definitions[id]); w=words+definitions[id];
  if(w[0]==((4u<<16)|43)) return w[3];
  if(w[0]==((4u<<16)|124))
    return origin_value(words,definitions,w[3],root,raw,load,base,depth+1);
  CHECK(w[0]==((5u<<16)|128) || w[0]==((5u<<16)|194));
  a=origin_value(words,definitions,w[3],root,raw,load,base,depth+1);
  b=origin_value(words,definitions,w[4],root,raw,load,base,depth+1);
  if((w[0]&65535)==128) return a+b;
  CHECK(b<32); return a>>b;
}
/* Follow actual output pointers back to the shared byte origin. Arbitrary
 * decoded pixels are irrelevant to this address-only check. No GPU/device. */
static void inspect_origin(const uint32_t *words, size_t count,
    const xe_test_resolve_options *options) {
  static uint32_t definitions[65536];
  uint32_t addresses[2], indices[2], common, root, origin, push, member, state=0x3187abcd;
  const uint32_t *p, *v, *add, *load, *pointer, *literal;
  unsigned stores=0, i, j, scaled=options->scale>1 && options->config!=2;
  size_t at, length;
  memset(definitions,0,sizeof(definitions));
  for(at=5;at<count;at+=length) {
    unsigned op=words[at]&65535; length=words[at]>>16;
    if(op==43 || op==59 || op==61 || op==65 || op==124 || op==128 || op==132 || op==194) {
      CHECK(length>=3 && words[at+2]<65536); definitions[words[at+2]]=(uint32_t)at;
    }
  }
  /* IDs below are checked explicitly before dereference. */
  for(at=5;at<count;at+=length) {
    length=words[at]>>16;
    if((words[at]&65535)!=62) continue;
    i=words[at+1]; CHECK(i<65536 && definitions[i]); p=words+definitions[i];
    if(p[0]!=((6u<<16)|65)) continue;
    CHECK(p[3]<65536 && definitions[p[3]]); v=words+definitions[p[3]];
    if(v[0]!=((4u<<16)|59) || v[3]!=2) continue;
    CHECK(stores<2 && p[5]<65536 && definitions[p[5]]); indices[stores]=p[5]; p=words+definitions[p[5]];
    CHECK(p[0]==((4u<<16)|124) && p[3]<65536 && definitions[p[3]]); p=words+definitions[p[3]];
    CHECK(p[0]==((5u<<16)|194) && p[4]<65536 && definitions[p[4]]);
    literal=words+definitions[p[4]]; CHECK(literal[0]==((4u<<16)|43) && literal[3]==4);
    addresses[stores++]=p[3];
  }
  CHECK(stores==2 && addresses[0]<65536 && definitions[addresses[0]]);
  common=addresses[0]; add=words+definitions[common];
  CHECK(add[0]==((5u<<16)|128) && add[4]<65536 && definitions[add[4]]);
  root=add[3]; load=words+definitions[add[4]];
  CHECK(load[0]==((4u<<16)|61) && load[3]<65536 && definitions[load[3]]);
  pointer=words+definitions[load[3]];
  CHECK(pointer[0]==((5u<<16)|65) && pointer[3]<65536 && definitions[pointer[3]]);
  push=pointer[3]; v=words+definitions[push]; CHECK(v[0]==((4u<<16)|59) && v[3]==9);
  member=pointer[4]; CHECK(member<65536 && definitions[member]);
  literal=words+definitions[member]; CHECK(literal[0]==((4u<<16)|43) && literal[3]==6);
  CHECK(addresses[1]<65536 && definitions[addresses[1]]); p=words+definitions[addresses[1]];
  CHECK(p[0]==((5u<<16)|128) && p[3]==common && p[4]<65536 && definitions[p[4]]);
  literal=words+definitions[p[4]]; CHECK(literal[0]==((4u<<16)|43));
  CHECK(literal[3]==(scaled?16u:32u));
  if(scaled) {
    CHECK(root<65536 && definitions[root]); p=words+definitions[root];
    CHECK(p[0]==((5u<<16)|128) && p[3]<65536 && definitions[p[3]]);
    v=words+definitions[p[3]]; CHECK(v[0]==((5u<<16)|132));
    CHECK(v[4]<65536 && definitions[v[4]]); literal=words+definitions[v[4]];
    CHECK(literal[0]==((4u<<16)|43));
    CHECK(literal[3]==options->scale*(options->scale==4?4u:3u));
  }
  for(i=0;i<64;++i) {
    uint32_t native, expected, actual;
    uint64_t old_byte_address;
    state^=state<<13; state^=state>>17; state^=state<<5;
    native=state&~UINT32_C(15); origin=i<4?i*16:(state>>4)&~UINT32_C(15);
    for(j=0;j<2;++j) {
      actual=origin_value(words,definitions,indices[j],root,native,load[2],origin,0)*16;
      old_byte_address=(uint64_t)native+origin+j*(scaled?16u:32u);
      expected=(uint32_t)((old_byte_address% (UINT64_C(1)<<32))/16*16);
      CHECK(actual==expected); ++origin_checks;
    }
  }
}
#endif
static int inspect(void *opaque, const uint32_t *words, size_t count) {
  context *c = (context *)opaque;
  char path[1024];
  FILE *file;
  size_t bytes = count * sizeof(uint32_t);
#ifndef XE_TEST_RESOLVE_DXIL
  size_t offset = 5;
  static unsigned char constants[65536];
  uint32_t product = 0;
  CHECK(count >= 5 && words[0] == 0x07230203);
  CHECK(words[3] <= sizeof(constants)); memset(constants, 0, sizeof(constants));
  calls = 0;
  while (offset < count) {
    uint32_t length = words[offset] >> 16;
    unsigned op = words[offset] & 65535;
    CHECK(length && length <= count - offset);
    if (op == 95) ++calls; /* OpImageFetch. */
    if (op == 43) { CHECK(length >= 4 && words[offset + 2] < words[3]); constants[words[offset + 2]] = 1; }
#ifndef XE_RESOLVE_LOADS_BASELINE
    if (op == 134 || op == 137) { /* OpUDiv, OpUMod: only literal divisors. */
      CHECK(length == 5 && words[offset + 4] < words[3] && constants[words[offset + 4]]);
    }
    if (op == 151) { CHECK(length == 5); product = words[offset + 2]; ++wide_multiplies; }
    if (op == 81 && length == 5 && words[offset + 3] == product) {
      CHECK(words[offset + 4] == 1); ++high_extracts;
    }
#else
    (void)product;
#endif
    offset += length;
  }
  CHECK(offset == count);
  if(c->options.direct && c->mode!=1) inspect_origin(words,count,&c->options);
#else
  CHECK(count > 8 && words[0] == 0x43425844); /* DXBC container. */
#endif
#ifndef XE_RESOLVE_LOADS_BASELINE
  CHECK(wide_multiplies == 1 && high_extracts == 1);
#endif
  if (calls != c->expected) {
    fprintf(stderr, "%u/%u/%u scale %u config %u direct %u: %u loads, wanted %u\n",
        c->options.format, c->options.depth, c->options.msaa, c->options.scale,
        c->options.config, c->options.direct, calls, c->expected); return 0;
  }
  path_make(path, sizeof(path), c->output, &c->options);
  file = fopen(path, "wb"); CHECK(file);
  CHECK(fwrite(words, 1, bytes, file) == bytes); CHECK(!fclose(file));
  if (c->baseline) {
    unsigned char scratch[4096];
    size_t consumed = 0;
    long baseline_bytes;
    path_make(path, sizeof(path), c->baseline, &c->options);
    file = fopen(path, "rb"); CHECK(file);
    CHECK(!fseek(file,0,SEEK_END)); baseline_bytes = ftell(file); CHECK(baseline_bytes > 0);
    CHECK(!fseek(file,0,SEEK_SET));
    if (c->options.direct && c->mode!=2) {
      CHECK(bytes < (size_t)baseline_bytes); ++reduced;
      CHECK(!fclose(file));
    }
#ifndef XE_TEST_RESOLVE_DXIL
    else if(c->mode==2 && c->options.direct && c->options.scale>1 && c->options.config!=2) {
      CHECK(bytes>(size_t)baseline_bytes); ++enlarged; CHECK(!fclose(file));
    }
#endif
    else {
    while (consumed < bytes) {
      size_t n = bytes - consumed;
      if (n > sizeof(scratch)) n = sizeof(scratch);
      CHECK(fread(scratch, 1, n, file) == n);
      CHECK(!memcmp(scratch, (const unsigned char *)words + consumed, n)); consumed += n;
    }
    CHECK(fgetc(file) == EOF && !ferror(file)); CHECK(!fclose(file)); ++compared;
    }
  }
  ++modules; binary_bytes += bytes; return 1;
}
int main(int argc, char **argv) {
  static const unsigned formats[14] = {0,1,2,3,4,5,6,7,10,12,14,15,0,1};
  static const unsigned scales[7] = {1,2,3,4,8,16,32};
  unsigned f, s, msaa, config, direct, volume;
  context c;
  CHECK(argc >= 2 && argc <= 4); memset(&c, 0, sizeof(c));
  c.output = argv[1]; c.baseline = argc >= 3 && argv[2][0] ? argv[2] : NULL;
  if(argc==4) { CHECK(!strcmp(argv[3],"legacy") || !strcmp(argv[3],"origin"));
    c.mode=!strcmp(argv[3],"legacy")?1u:2u; }
  for (f = 0; f < 14; ++f) for (msaa = 0; msaa < 3; ++msaa)
  for (s = 0; s < 7; ++s) for (config = 0; config < 3; ++config)
  for (direct = 0; direct < 2; ++direct) for (volume = 0; volume <= direct; ++volume) {
    unsigned run = f < 12 && (formats[f] == 5 || formats[f] == 7 || formats[f] == 15) ? 4 : 8;
#ifdef XE_TEST_RESOLVE_DXIL
    if (scales[s] > 16) continue; /* Native DXIL emitter rejects larger scales. */
#endif
    c.options.format = formats[f]; c.options.depth = f >= 12; c.options.msaa = msaa;
    c.options.scale = scales[s]; c.options.config = config; c.options.direct = direct; c.options.volume = volume;
    wide_multiplies = 0; high_extracts = 0;
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
      options.scale_x = scales[s]; options.scale_y = scales[s] == 1 ? 1 : (scales[s] == 4 ? 4 : 3);
      options.native_source = config != 0; options.native_layout = config == 2;
      options.direct_resolve = direct; options.direct_resolve_3d = volume; options.msaa_2x_supported = config != 0;
      options.source_uint = !options.depth && ((options.format >= 4 && options.format <= 7) || options.format >= 14);
      options.round_depth = 1; calls = 0; wide_product = NULL;
      CHECK(xe_dxil_build_edram_dump(&options, &binary)); CHECK(!(binary.size % 4));
      CHECK(inspect(&c, (const uint32_t *)binary.data, binary.size / 4));
      xe_dxil_shader_binary_free(&binary);
    }
#else
    CHECK(xe_test_resolve_spv(&c.options, inspect, &c));
#endif
  }
  printf("actual " EXTENSION " emitters: %u modules, %u modules byte-identical, %u direct modules smaller, %lu total bytes; load counts PASS\n",
      modules, compared, reduced, (unsigned long)binary_bytes);
  printf("origin checks: %u store addresses, %u modules enlarged for scaled offset\n",origin_checks,enlarged);
  return 0;
}
