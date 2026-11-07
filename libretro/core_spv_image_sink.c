#include "core_spv_image_sink.h"
#include "core_gpu_divide.h"
#include <stdlib.h>
#include <string.h>
#ifndef XE_SPV_SINK_REALLOC
#define XE_SPV_SINK_REALLOC realloc
#endif
#ifndef XE_SPV_SINK_FREE
#define XE_SPV_SINK_FREE free
#endif

enum { SET_ZERO = 1, BIND_ZERO = 2, DEST_POINTER = 4 };
typedef struct sink {
  xe_spv_sink_workspace *work;
  xe_spv_binary *out;
  const uint32_t *words;
  size_t count;
  uint32_t bound, next, uint_type, bool_type, dest, push, push_type;
  uint32_t image_type, image_pointer, push_pointer, v3, v4, pair;
  uint32_t zero, one, two, three, four, six, eight, nine, ten, sixteen, mask;
  uint32_t group_values[4], group_root, group_delta, group_index;
  unsigned group_count, groups, bytes;
} sink;
static const uint32_t *definition(sink *s, uint32_t id) {
  uint32_t offset;
  if (!id || id >= s->bound) return NULL;
  offset = s->work->entries[id].offset;
  return offset ? s->words + offset : NULL;
}
static int literal(sink *s, uint32_t id, uint32_t *value) {
  const uint32_t *w = definition(s, id);
  if (!w || w[0] != ((4u << 16) | 43)) return 0;
  *value = w[3]; return 1;
}
static uint32_t fresh(sink *s) {
  if (s->next == UINT32_MAX) { s->out->failed = 1; return 0; }
  return s->next++;
}
static uint32_t instruction(sink *s, unsigned op, uint32_t type,
    const uint32_t *args, size_t count) {
  uint32_t id = fresh(s);
  xe_spv_binary_instruction(s->out, op, type, id, args, count);
  return s->out->failed ? 0 : id;
}
static uint32_t constant(sink *s, uint32_t value) {
  return instruction(s, 43, s->uint_type, &value, 1);
}
static uint32_t binary(sink *s, unsigned op, uint32_t type, uint32_t a, uint32_t b) {
  uint32_t args[2]; args[0] = a; args[1] = b;
  return instruction(s, op, type, args, 2);
}
static uint32_t load_constant(sink *s, uint32_t member) {
  uint32_t args[2], pointer;
  args[0] = s->push; args[1] = member;
  pointer = instruction(s, 65, s->push_pointer, args, 2);
  return instruction(s, 61, s->uint_type, &pointer, 1);
}
static uintptr_t divide_emit(void *opaque, enum xe_gpu_divide_op op,
    uintptr_t a, uintptr_t b, uintptr_t c) {
  sink *s = (sink *)opaque;
  uint32_t args[3], product;
  args[0] = (uint32_t)a; args[1] = (uint32_t)b; args[2] = (uint32_t)c;
  switch (op) {
    case XE_GPU_DIV_CONST: return a ? s->one : s->zero;
    case XE_GPU_DIV_MUL_HIGH:
      product = instruction(s, 151, s->pair, args, 2);
      args[0] = product; args[1] = 1;
      return instruction(s, 81, s->uint_type, args, 2);
    case XE_GPU_DIV_EQ: return instruction(s, 170, s->bool_type, args, 2);
    case XE_GPU_DIV_GE: return instruction(s, 174, s->bool_type, args, 2);
    case XE_GPU_DIV_SELECT: return instruction(s, 169, s->uint_type, args, 3);
    case XE_GPU_DIV_MUL: return instruction(s, 132, s->uint_type, args, 2);
    case XE_GPU_DIV_SUB: return instruction(s, 130, s->uint_type, args, 2);
    case XE_GPU_DIV_ADD: return instruction(s, 128, s->uint_type, args, 2);
  }
  return 0;
}
/* Decompose constant additions only; never equate arbitrary SSA values. */
static int byte_address(sink *s, uint32_t index, uint32_t *root, uint32_t *delta) {
  const uint32_t *w = definition(s, index);
  uint32_t value, a, b;
  unsigned steps = 0;
  if (!w || w[0] != ((5u << 16) | 134) || !literal(s, w[4], &value) || value != 4) return 0;
  *root = w[3]; *delta = 0;
  while ((w = definition(s, *root)) != NULL && w[0] == ((5u << 16) | 128)) {
    if (++steps > 64) return 0;
    a = w[3]; b = w[4];
    if (literal(s, b, &value)) { *root = a; *delta += value; }
    else if (literal(s, a, &value)) { *root = b; *delta += value; }
    else break;
  }
  return *root && *root < s->bound;
}
static int image_group(sink *s) {
  uintptr_t q, r;
  uint32_t pitch, reciprocal, n, y, x, layer, coord, texel, image;
  uint32_t args[4], values[4], word;
  unsigned i, j, pixels = 16 / s->bytes;
  n = binary(s, 194, s->uint_type, s->group_index, s->two);
  /* Host pitch (member6) is in bytes, divisible by the 16-byte output run. */
  pitch = binary(s, 194, s->uint_type, load_constant(s, s->six), s->four);
  reciprocal = load_constant(s, s->ten);
  if (!xe_gpu_divide_emit(divide_emit, s, n, pitch, reciprocal, &q, &r)) return 0;
  y = binary(s, 128, s->uint_type, (uint32_t)q, load_constant(s, s->eight));
  layer = load_constant(s, s->nine);
  image = instruction(s, 61, s->image_type, &s->dest, 1);
  x = (uint32_t)r;
  if (pixels != 1) x = binary(s, 132, s->uint_type, x, pixels == 4 ? s->four : s->two);
  for (i = 0; i < pixels; ++i) {
    args[0] = i ? binary(s, 128, s->uint_type, x, i == 1 ? s->one : (i == 2 ? s->two : s->three)) : x;
    args[1] = y; args[2] = layer;
    coord = instruction(s, 80, s->v3, args, 3);
    if (s->bytes == 4) {
      values[0] = s->group_values[i]; values[1] = values[2] = values[3] = s->zero;
    } else if (s->bytes == 8) {
      /* RGBA16_UINT is the existing size-compatible 64-bit staging format. */
      for (j = 0; j < 4; ++j) {
        word = s->group_values[i * 2 + j / 2];
        values[j] = j & 1 ? binary(s, 194, s->uint_type, word, s->sixteen)
                          : binary(s, 199, s->uint_type, word, s->mask);
      }
    } else memcpy(values, s->group_values, sizeof(values));
    texel = instruction(s, 80, s->v4, values, 4);
    args[0] = image; args[1] = coord; args[2] = texel;
    xe_spv_binary_instruction(s->out, 99, 0, 0, args, 3);
  }
  return !s->out->failed;
}
static int store(sink *s, const uint32_t *w, int emit) {
  const uint32_t *pointer = definition(s, w[1]);
  uint32_t root, delta;
  if ((w[0] >> 16) != 3 || !pointer || (pointer[0] >> 16) != 6 ||
      !byte_address(s, pointer[5], &root, &delta)) return 0;
  if (!s->group_count) {
    s->group_root = root; s->group_delta = delta; s->group_index = pointer[5];
  } else if (s->group_root != root || delta != s->group_delta + s->group_count * 4) return 0;
  s->group_values[s->group_count++] = w[2];
  if (s->group_count == 4) {
    if (emit && !image_group(s)) return 0;
    ++s->groups; s->group_count = 0;
  }
  return 1;
}
static int globals(sink *s) {
  uint32_t args[7];
  args[0] = s->uint_type; args[1] = 1; args[2] = 0; args[3] = 1;
  args[4] = 0; args[5] = 2; args[6] = s->bytes == 4 ? 33 : (s->bytes == 8 ? 31 : 30);
  s->image_type = instruction(s, 25, 0, args, 7);
  args[0] = 0; args[1] = s->image_type;
  s->image_pointer = instruction(s, 32, 0, args, 2);
  args[0] = 9; args[1] = s->uint_type;
  if (!s->push_pointer) s->push_pointer = instruction(s, 32, 0, args, 2);
  args[0] = s->uint_type; args[1] = 3;
  if (!s->v3) s->v3 = instruction(s, 23, 0, args, 2);
  args[1] = 4; if (!s->v4) s->v4 = instruction(s, 23, 0, args, 2);
  args[1] = s->uint_type; s->pair = instruction(s, 30, 0, args, 2);
  s->zero = constant(s, 0); s->one = constant(s, 1); s->two = constant(s, 2);
  s->three = constant(s, 3); s->four = constant(s, 4); s->six = constant(s, 6);
  s->eight = constant(s, 8); s->nine = constant(s, 9);
  s->ten = constant(s, 10); s->sixteen = constant(s, 16); s->mask = constant(s, 65535);
  return !s->out->failed;
}
void xe_spv_sink_close(xe_spv_sink_workspace *work) {
  XE_SPV_SINK_FREE(work->entries); memset(work, 0, sizeof(*work));
}
int xe_spv_image_sink(xe_spv_sink_workspace *work, xe_spv_binary *out,
    const uint32_t *words, size_t count, unsigned bytes) {
  sink s;
  size_t at, j, length, reserve;
  unsigned op, result;
  int in_function = 0;
  uint32_t id, value, args[4], member_mask = 0;
  const uint32_t *w, *ptr;
  xe_spv_sink_entry *entries;
  if (!work || !out) return 0;
  xe_spv_binary_reset(out);
  if (!words || count < 5 || count > UINT32_MAX || count > (size_t)-1 / 4 - 128 || words[0] != 0x07230203 ||
      !words[3] || words[3] > 4194304 || (bytes != 4 && bytes != 8 && bytes != 16)) return 0;
  memset(&s, 0, sizeof(s)); s.work = work; s.out = out; s.words = words;
  s.count = count; s.bound = words[3]; s.next = s.bound; s.bytes = bytes;
  if (s.bound > work->capacity) {
    entries = (xe_spv_sink_entry *)XE_SPV_SINK_REALLOC(work->entries, s.bound * sizeof(*entries));
    if (!entries) return 0;
    work->entries = entries; work->capacity = s.bound;
  }
  memset(work->entries, 0, s.bound * sizeof(*work->entries));
  for (at = 5; at < count; at += length) {
    w = words + at; length = w[0] >> 16; op = w[0] & 65535;
    if (!length || length > count - at) goto fail;
    result = 0;
    if (op >= 19 && op <= 39) result = 1;
    else if ((op >= 43 && op <= 52) || op == 59 || op == 61 || op == 65 || op == 128 || op == 134) result = 2;
    if (result) {
      if (length <= result || !w[result] || w[result] >= s.bound || work->entries[w[result]].offset) goto fail;
      work->entries[w[result]].offset = (uint32_t)at;
    }
    if (op == 21 && length == 4 && w[2] == 32 && !w[3]) s.uint_type = w[1];
    if (op == 20 && length == 2) s.bool_type = w[1];
    if (op == 71 && length == 4 && w[1] < s.bound && !w[3]) {
      if (w[2] == 33) work->entries[w[1]].flags |= BIND_ZERO;
      if (w[2] == 34) work->entries[w[1]].flags |= SET_ZERO;
    }
    if (op == 59 && length >= 4 && w[3] == 9) {
      if (s.push) goto fail;
      s.push = w[2];
    }
  }
  if (!s.uint_type || !s.bool_type || !s.push) goto fail;
  for (id = 1; id < s.bound; ++id) {
    if ((work->entries[id].flags & (SET_ZERO | BIND_ZERO)) == (SET_ZERO | BIND_ZERO)) {
      if (s.dest) goto fail;
      w = definition(&s, id);
      if (!w || (w[0] & 65535) != 59 || (w[0] >> 16) != 4 || w[3] != 12) goto fail;
      s.dest = id;
    }
  }
  if (!s.dest) goto fail;
  w = definition(&s, s.push); ptr = definition(&s, w[1]);
  if (!ptr || ptr[0] != ((4u << 16) | 32) || ptr[2] != 9) goto fail;
  s.push_type = ptr[3]; w = definition(&s, s.push_type);
  if (!w || w[0] != ((10u << 16) | 30)) goto fail;
  for (at = 5; at < count; at += length) {
    w = words + at; length = w[0] >> 16; op = w[0] & 65535;
    if (op == 23 && length == 4 && w[2] == s.uint_type) {
      if (w[3] == 3) s.v3 = w[1];
      if (w[3] == 4) s.v4 = w[1];
    }
    if (op == 32 && length == 4 && w[2] == 9 && w[3] == s.uint_type) s.push_pointer = w[1];
    if (op == 72 && length == 5 && w[1] == s.push_type && w[3] == 35) {
      static const uint32_t offsets[8] = {0,4,8,12,16,28,32,36};
      if (w[2] >= 8 || w[4] != offsets[w[2]]) goto fail;
      member_mask |= 1u << w[2];
    }
    if (op == 65 && length >= 4 && w[3] == s.dest) {
      if (length != 6 || !literal(&s, w[4], &value) || value) goto fail;
      work->entries[w[2]].flags |= DEST_POINTER;
    }
  }
  if (member_mask != 255) goto fail;
  /* Validate all output groups and reject function pointer aliases, loads and
   * atomics. Global literal declarations are not ID operands. This pass is
   * for the core's validated shader templates, not an untrusted SPIR-V loader. */
  for (at = 5; at < count; at += length) {
    w = words + at; length = w[0] >> 16; op = w[0] & 65535;
    if (op == 54) in_function = 1;
    if (!in_function) continue;
    for (j = 1; j < length; ++j) {
      if ((op == 12 && j == 4) || (op == 79 && j >= 5) ||
          (op == 81 && j >= 4) || (op == 82 && j >= 5)) continue;
      id = w[j];
      if (id >= s.bound) continue;
      if (work->entries[id].flags & DEST_POINTER) {
        if (!((op == 65 && j == 2) || ((op == 5 || op == 71 || op == 62) && j == 1))) goto fail;
      }
      if (id == s.dest && !(op == 15 || ((op == 5 || op == 71) && j == 1) ||
          (op == 59 && j == 2) || (op == 65 && j == 3))) goto fail;
    }
    if (op == 62 && length >= 3 && w[1] < s.bound && (work->entries[w[1]].flags & DEST_POINTER)) {
      if (!store(&s, w, 0)) goto fail;
    }
    if (s.group_count && ((op >= 248 && op <= 255) || op == 56 || op == 224 || op == 225)) goto fail;
    if (op == 56) in_function = 0;
  }
  if (!s.groups || s.group_count || s.groups > ((size_t)-1 - count - 128) / 512) goto fail;
  reserve = count + 128 + (size_t)s.groups * 512;
  if (!xe_spv_binary_reserve(out, reserve)) goto fail;
  xe_spv_binary_append(out, words, 5);
  s.groups = 0;
  for (at = 5; at < count; at += length) {
    w = words + at; length = w[0] >> 16; op = w[0] & 65535;
    if ((op == 5 || op == 71) && length >= 2 && w[1] < s.bound &&
        (work->entries[w[1]].flags & DEST_POINTER)) continue;
    if (op == 65 && length == 6 && w[3] == s.dest) continue;
    if (op == 62 && length >= 3 && w[1] < s.bound && (work->entries[w[1]].flags & DEST_POINTER)) {
      if (!store(&s, w, 1)) goto fail;
      continue;
    }
    if (op == 71 && length == 4 && w[1] == s.dest && w[2] == 33) {
      args[0] = s.dest; args[1] = 33; args[2] = 1;
      xe_spv_binary_instruction(out, 71, 0, 0, args, 3); continue;
    }
    if (op == 59 && w[2] == s.dest) {
      args[0] = 0;
      xe_spv_binary_instruction(out, 59, s.image_pointer, s.dest, args, 1); continue;
    }
    if (op == 30 && w[1] == s.push_type) {
      xe_spv_binary_word(out, (uint32_t)((length + 3) << 16) | op);
      xe_spv_binary_append(out, w + 1, length - 1);
      xe_spv_binary_word(out, s.uint_type); xe_spv_binary_word(out, s.uint_type); xe_spv_binary_word(out, s.uint_type);
      continue;
    }
    if (op >= 19 && op <= 39) {
      /* Only before the first type; emitted decorations must precede types. */
      if (!s.image_type) {
        for (j = 0; j < 3; ++j) {
          args[0] = s.push_type; args[1] = (uint32_t)(8 + j); args[2] = 35;
          args[3] = (uint32_t)(40 + 4 * j);
          xe_spv_binary_instruction(out, 72, 0, 0, args, 4);
        }
        /* Nonzero sentinel until the unsigned scalar type is declared. */
        s.image_type = UINT32_MAX;
      }
    }
    xe_spv_binary_append(out, w, length);
    if (op == 21 && w[1] == s.uint_type && !globals(&s)) goto fail;
  }
  if (out->failed || s.group_count) goto fail;
  out->words[3] = s.next;
  return 1;
fail:
  xe_spv_binary_reset(out);
  return 0;
}
