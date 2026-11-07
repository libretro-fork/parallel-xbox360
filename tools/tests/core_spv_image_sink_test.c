/* Actual template rewrite, borrowed instruction equivalence and numeric sink
 * interpretation. CPU only, MSVC C89. No Vulkan device or decoder mock. */
#include "core_spv_image_sink.h"
#include "core_spv_image_sink_alloc.h"
#include "core_texture_image_atlas.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s line %u: %s\n",current,(unsigned)__LINE__,#x); exit(1); } } while (0)
static const char *current = "atlas";
static unsigned allocation_calls, fail_at, live_allocations;
void *xe_test_sink_realloc(void *old, size_t bytes) {
  void *result;
  int fresh = old == NULL;
  CHECK(bytes);
  if (++allocation_calls == fail_at) return NULL;
  result = realloc(old,bytes);
  if (result && fresh) ++live_allocations;
  return result;
}
void xe_test_sink_free(void *allocation) {
  if (allocation) { CHECK(live_allocations); --live_allocations; free(allocation); }
}
typedef struct value { uint32_t v[4]; } value;
typedef struct group { uint32_t index, words[4]; } group;
typedef struct fixture { const char *name; unsigned bytes; } fixture;
static const fixture fixtures[] = {
  {"128bpb",16},{"128bpb_scaled",16},{"32bpb",4},{"32bpb_scaled",4},
  {"64bpb",8},{"64bpb_scaled",8},{"bgrg8_rgb8",8},{"bgrg8_rgbg8",4},
  {"gbgr8_rgb8",8},{"gbgr8_grgb8",4},{"depth_float",4},{"depth_float_scaled",4},
  {"depth_unorm",4},{"depth_unorm_scaled",4},{"dxt1_rgba8",4},
  {"dxt3_rgba8",4},{"dxt5_rgba8",4},
  {"r10g11b11_rgba16",8},{"r10g11b11_rgba16_scaled",8},
  {"r10g11b11_rgba16_snorm",8},{"r10g11b11_rgba16_snorm_scaled",8},
  {"r11g11b10_rgba16",8},{"r11g11b10_rgba16_scaled",8},
  {"r11g11b10_rgba16_snorm",8},{"r11g11b10_rgba16_snorm_scaled",8},
  {"rg16_snorm_float",4},{"rg16_snorm_float_scaled",4},
  {"rg16_unorm_float",4},{"rg16_unorm_float_scaled",4},
  {"rgba16_snorm_float",8},{"rgba16_snorm_float_scaled",8},
  {"rgba16_unorm_float",8},{"rgba16_unorm_float_scaled",8}
};
static unsigned modules, interpretations, image_writes;
static uint32_t *read_module(const char *path, size_t *count) {
  FILE *f = fopen(path,"rb");
  long bytes;
  uint32_t *words;
  CHECK(f && !fseek(f,0,SEEK_END)); bytes = ftell(f);
  CHECK(bytes >= 20 && !(bytes % 4) && !fseek(f,0,SEEK_SET));
  words = (uint32_t *)malloc((size_t)bytes); CHECK(words);
  CHECK(fread(words,1,(size_t)bytes,f) == (size_t)bytes && !fclose(f));
  *count = (size_t)bytes / 4; return words;
}
static void interpret(const uint32_t *input, size_t count, const xe_spv_binary *out,
    const uint32_t *pointers, uint32_t dest, uint32_t push, unsigned bytes) {
  group groups[256];
  size_t at;
  unsigned length, op, stores = 0, n_groups, test, i, g, pixel, component;
  uint32_t id, divisor, reciprocal, origin, layer, a, b, result, expected;
  value *values = (value *)calloc(out->words[3],sizeof(value));
  const uint32_t *w;
  CHECK(values);
  for (at = 5; at < count; at += length) {
    w = input + at; length = w[0] >> 16;
    if ((w[0] & 65535) == 62 && pointers[w[1]]) {
      CHECK(stores / 4 < 256);
      if (!(stores % 4)) groups[stores/4].index = input[pointers[w[1]] + 5];
      groups[stores/4].words[stores%4] = w[2]; ++stores;
    }
  }
  CHECK(stores && !(stores % 4)); n_groups = stores / 4;
  for (test = 0; test < 128; ++test) {
    divisor = test < 8 ? (1u << test) : test * 7 + 3;
    CHECK(xe_gpu_divide_prepare(divisor,&reciprocal));
    origin = test * 3; layer = test % 6;
    for (id = 1; id < input[3]; ++id) values[id].v[0] = id * 0x9e3779b9u + test * 0xabcdef01u;
    for (at = 5; at < count; at += length) {
      w = input + at; length = w[0] >> 16;
      if (w[0] == ((4u << 16) | 43)) values[w[2]].v[0] = w[3];
    }
    for (i = 0; i < n_groups; ++i)
      values[groups[i].index].v[0] = (i * 0x243f6a88u + test * 0x13579bdfu) & ~3u;
    g = 0; pixel = 0;
    for (at = 5; at < out->count; at += length) {
      w = out->words + at; length = w[0] >> 16; op = w[0] & 65535;
      if (op == 99) {
        const value *coord, *texel;
        uint32_t n;
        CHECK(g < n_groups && length == 4);
        coord = &values[w[2]]; texel = &values[w[3]];
        n = values[groups[g].index].v[0] / 4;
        CHECK(coord->v[0] == n % divisor * (16 / bytes) + pixel);
        CHECK(coord->v[1] == n / divisor + origin && coord->v[2] == layer);
        for (component = 0; component < 4; ++component) {
          if (bytes == 4) expected = component ? 0 : values[groups[g].words[pixel]].v[0];
          else if (bytes == 8) expected = (values[groups[g].words[pixel*2+component/2]].v[0] >> ((component & 1) * 16)) & 65535;
          else expected = values[groups[g].words[component]].v[0];
          CHECK(texel->v[component] == expected);
        }
        if (++pixel == 16 / bytes) { pixel = 0; ++g; }
        ++image_writes; continue;
      }
      if (op == 43 && length == 4 && w[2] >= input[3]) { values[w[2]].v[0] = w[3]; continue; }
      if (!(op == 61 || op == 65 || op == 80 || op == 81 || op == 128 || op == 130 ||
          op == 132 || op == 151 || op == 169 || op == 170 || op == 174 || op == 194 || op == 199)) continue;
      if (w[2] < input[3]) continue; /* Original decode is unchanged, supplied as arbitrary input. */
      id = w[2]; a = values[w[3]].v[0]; b = length > 4 ? values[w[4]].v[0] : 0;
      result = 0;
      switch (op) {
        case 65: CHECK(w[3] == push && length == 5); result = b; break;
        case 61:
          if (w[3] == dest) break;
          CHECK(a == 6 || a == 8 || a == 9 || a == 10);
          result = a == 6 ? divisor * 16 : (a == 8 ? origin : (a == 9 ? layer : reciprocal)); break;
        case 80:
          CHECK(length == 6 || length == 7);
          for (i = 0; i < length - 3; ++i) values[id].v[i] = values[w[3+i]].v[0];
          continue;
        case 81: CHECK(length == 5 && w[4] < 4); result = values[w[3]].v[w[4]]; break;
        case 128: result = a + b; break;
        case 130: result = a - b; break;
        case 132: result = a * b; break;
        case 151:
          values[id].v[1] = (uint32_t)(((uint64_t)a * b) >> 32); result = a*b; break;
        case 169: CHECK(length == 6); result = a ? b : values[w[5]].v[0]; break;
        case 170: result = a == b; break;
        case 174: result = a >= b; break;
        case 194: CHECK(b < 32); result = a >> b; break;
        case 199: result = a & b; break;
        default: CHECK(0);
      }
      values[id].v[0] = result;
    }
    CHECK(g == n_groups && !pixel); ++interpretations;
  }
  free(values);
}
static void check_module(xe_spv_sink_workspace *work, xe_spv_binary *out,
    uint32_t *input, size_t count, unsigned bytes) {
  static const char dest_name[] = "xe_texture_load_dest";
  uint32_t dest = 0, push = 0, push_type = 0;
  uint32_t *pointers = (uint32_t *)calloc(input[3],sizeof(uint32_t));
  size_t at, cursor = 5, length, n, k;
  const uint32_t *w;
  uint32_t saved;
  unsigned failure, before_live = live_allocations;
  for (failure = 1; failure <= 2; ++failure) {
    xe_spv_sink_workspace failed_work = {0}; xe_spv_binary failed_out = {0};
    allocation_calls = 0; fail_at = failure;
    CHECK(!xe_spv_image_sink(&failed_work,&failed_out,input,count,bytes));
    CHECK(allocation_calls == failure && !failed_out.count);
    xe_spv_sink_close(&failed_work); xe_spv_binary_close(&failed_out);
    CHECK(live_allocations == before_live);
  }
  fail_at = 0;
  {
    xe_spv_sink_workspace retained_work = {0}; xe_spv_binary retained_out = {0};
    uint32_t *retained_words;
    xe_spv_sink_entry *retained_entries;
    allocation_calls = 0;
    CHECK(xe_spv_image_sink(&retained_work,&retained_out,input,count,bytes));
    CHECK(allocation_calls == 2);
    retained_words = retained_out.words; retained_entries = retained_work.entries;
    CHECK(xe_spv_image_sink(&retained_work,&retained_out,input,count,bytes));
    CHECK(allocation_calls == 2 && retained_words == retained_out.words &&
        retained_entries == retained_work.entries);
    xe_spv_sink_close(&retained_work); xe_spv_binary_close(&retained_out);
    CHECK(live_allocations == before_live);
  }
  CHECK(pointers && xe_spv_image_sink(work,out,input,count,bytes));
  for (at = 5; at < count; at += length) {
    w = input + at; length = w[0] >> 16;
    if ((w[0] & 65535) == 5 && (length-2)*4 >= sizeof(dest_name) && !memcmp(w+2,dest_name,sizeof(dest_name))) dest = w[1];
    if ((w[0] & 65535) == 59 && w[3] == 9) push = w[2];
  }
  CHECK(dest && push);
  for (at = 5; at < count; at += length) {
    w = input + at; length = w[0] >> 16;
    if ((w[0] & 65535) == 65 && w[3] == dest) pointers[w[2]] = (uint32_t)at;
    if ((w[0] & 65535) == 59 && w[2] == push) {
      for (k = 5; k < count; k += input[k] >> 16)
        if ((input[k] & 65535) == 32 && input[k+1] == w[1]) push_type = input[k+3];
    }
  }
  CHECK(push_type);
  /* Every original decode, source load, conversion and control-flow instruction
   * survives byte-for-byte in the same order, except the explicit output ABI. */
  for (at = 5; at < count; at += length) {
    unsigned op;
    w = input + at; length = w[0] >> 16; op = w[0] & 65535;
    if ((op == 62 && pointers[w[1]]) || (op == 65 && pointers[w[2]]) ||
        ((op == 5 || op == 71) && pointers[w[1]]) ||
        (op == 59 && w[2] == dest) || (op == 30 && w[1] == push_type) ||
        (op == 71 && length == 4 && w[1] == dest && w[2] == 33)) continue;
    while (cursor < out->count) {
      n = out->words[cursor] >> 16; CHECK(n && n <= out->count-cursor);
      if (n == length && !memcmp(w,out->words+cursor,n*4)) break;
      cursor += n;
    }
    CHECK(cursor < out->count); cursor += length;
  }
  interpret(input,count,out,pointers,dest,push,bytes);
  /* Reject truncated instruction streams and unsupported block widths. */
  for (k = 0; k < 16; ++k) { CHECK(!xe_spv_image_sink(work,out,input,k,bytes)); CHECK(!out->count); }
  CHECK(!xe_spv_image_sink(work,out,input,count,2) && !out->count);
  saved = input[3]; input[3] = 0;
  CHECK(!xe_spv_image_sink(work,out,input,count,bytes) && !out->count); input[3] = saved;
  saved = input[5]; input[5] &= 65535;
  CHECK(!xe_spv_image_sink(work,out,input,count,bytes) && !out->count); input[5] = saved;
  /* Breaking a tuple must never publish a partial image shader. */
  for (at = 5; at < count; at += input[at] >> 16) {
    if ((input[at] & 65535) == 62 && pointers[input[at+1]]) {
      saved = input[at]; input[at] = (saved & 0xffff0000u) | 249;
      CHECK(!xe_spv_image_sink(work,out,input,count,bytes) && !out->count); input[at] = saved; break;
    }
  }
  CHECK(xe_spv_image_sink(work,out,input,count,bytes));
  /* Structural mutations are never submitted to a driver. Sanitizers check
   * the bounded template scanner even when a mutation remains rewriteable. */
  for (k = 0; k < 500; ++k) {
    size_t position = (k * 997 + 5) % count;
    int result;
    saved = input[position]; input[position] ^= 1u << (k % 32);
    result = xe_spv_image_sink(work,out,input,count,bytes);
    CHECK(result || !out->count); input[position] = saved;
  }
  CHECK(xe_spv_image_sink(work,out,input,count,bytes));
  free(pointers); ++modules;
}
static void atlas_test(void) {
  xe_texture_image_atlas atlas = {0,0}, before;
  xe_texture_image_level level, saved;
  uint32_t x = 99, y = 98, b, width, height, row, column;
  unsigned trials = 0;
  memset(&level,0,sizeof(level));
  for (b = 4; b <= 16; b *= 2) {
    for (width = 16/b; width <= 2048; width *= 2) {
      for (height = 1; height <= 128; height *= 2) {
        atlas.width = 0; atlas.height = 7;
        CHECK(xe_texture_image_add(&atlas,width,height,b,16384,&level));
        CHECK(level.y == 7 && atlas.width == width && atlas.height == height+7);
        for (row = 0; row < height; row += height/2+1)
          for (column = 0; column < width; column += width/2+1) {
            CHECK(xe_texture_image_origin(&level,((uint64_t)row*width+column)*b,b,
                width-column,height-row,&x,&y));
            CHECK(x == column && y == row+7); ++trials;
          }
        CHECK(!xe_texture_image_origin(&level,(uint64_t)width*height*b,b,1,1,&x,&y));
        CHECK(!xe_texture_image_origin(&level,(uint64_t)(width-1)*b,b,2,1,&x,&y));
      }
    }
  }
  before = atlas; saved = level;
  CHECK(!xe_texture_image_add(&atlas,UINT32_MAX,1,16,16384,&level));
  CHECK(!xe_texture_image_add(&atlas,1,1,4,16384,&level));
  CHECK(!xe_texture_image_add(&atlas,16,UINT32_MAX,4,16384,&level));
  CHECK(!xe_texture_image_add(&atlas,16,1,2,16384,&level));
  CHECK(!memcmp(&before,&atlas,sizeof(atlas)) && !memcmp(&saved,&level,sizeof(level)));
  x = 99; y = 98;
  CHECK(!xe_texture_image_origin(&level,UINT64_MAX,8,1,1,&x,&y));
  CHECK(x == 99 && y == 98);
  printf("PASS: %u atlas/packed-region bounds\n",trials);
}
int main(int argc, char **argv) {
  xe_spv_sink_workspace work = {0}; xe_spv_binary out = {0};
  unsigned i;
  char source[512], dest[512];
  FILE *f;
  CHECK(argc == 2); atlas_test();
  for (i = 0; i < sizeof(fixtures)/sizeof(fixtures[0]); ++i) {
    uint32_t *input; size_t count;
    current = fixtures[i].name;
    CHECK(snprintf(source,sizeof(source),"src/xenia/gpu/shaders/bytecode/vulkan_spirv/texture_load_%s_cs.spv",current) > 0);
    input = read_module(source,&count);
    check_module(&work,&out,input,count,fixtures[i].bytes);
    CHECK(snprintf(dest,sizeof(dest),"%s/%s.spv",argv[1],current) > 0);
    f = fopen(dest,"wb"); CHECK(f && fwrite(out.words,4,out.count,f) == out.count && !fclose(f));
    free(input);
  }
  xe_spv_binary_close(&out); xe_spv_sink_close(&work);
  CHECK(!work.entries && !work.capacity && !out.words && !out.count);
  CHECK(!live_allocations);
  xe_spv_binary_close(&out); xe_spv_sink_close(&work);
  printf("PASS: %u actual shader templates, %u sink interpretations, %u image stores\n",modules,interpretations,image_writes);
  return 0;
}
