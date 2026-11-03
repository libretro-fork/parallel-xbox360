#include "core_zarchive.h"
#include "core_file.h"
#include <stdlib.h>
#include <string.h>
#include <rthreads/rthreads.h>
#include <encodings/rzstd.h>

#define BLOCK_SIZE 65536U
#define CACHE_COUNT 64U
#define HASH_COUNT 128U
#define NO_SLOT CACHE_COUNT
#if defined(_MSC_VER)
#define ZAR_NOINLINE __declspec(noinline)
#elif defined(__GNUC__)
#define ZAR_NOINLINE __attribute__((noinline))
#else
#define ZAR_NOINLINE
#endif
typedef struct zar_section {
  uint64_t offset, size;
  const uint8_t *data;
  uint8_t *owned;
} zar_section;
typedef struct zar_cache {
  uint64_t key;
  const uint8_t *view;
  unsigned prev, next;
} zar_cache;
struct xe_zar {
  xe_file *file;
  slock_t *lock;
  zar_section sections[4];
  uint32_t nodes;
  uint64_t blocks;
  const uint8_t *mapping;
  uint8_t *cache_data, *compressed;
  rzstd_decoder_t *decoder;
  zar_cache cache[CACHE_COUNT];
  unsigned hash[HASH_COUNT], first, last;
};
static uint32_t be32(const uint8_t *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
      ((uint32_t)p[2] << 8) | p[3];
}
static uint64_t be64(const uint8_t *p) {
  return ((uint64_t)be32(p) << 32) | be32(p + 4);
}
static const uint8_t *node_ptr(const xe_zar *r, uint32_t node) {
  return r->sections[3].data + (size_t)node * 16;
}
int xe_zar_type(const xe_zar *r, uint32_t node) {
  if (!r || node >= r->nodes) return -1;
  return (node_ptr(r, node)[0] & 0x80) != 0;
}
static uint64_t node_offset(const uint8_t *n) {
  return be32(n + 4) | ((uint64_t)(be32(n + 12) & 0xffff) << 32);
}
static uint64_t node_size(const uint8_t *n) {
  return be32(n + 8) | ((uint64_t)(be32(n + 12) & 0xffff0000U) << 16);
}
static int name_get(const xe_zar *r, uint32_t node, const char **name, size_t *size) {
  const zar_section *s = &r->sections[2];
  uint32_t offset = be32(node_ptr(r, node)) & 0x7fffffffU;
  size_t length;
  *name = ""; *size = 0;
  if (offset == 0x7fffffffU) return node == 0;
  if (offset >= s->size) return 0;
  length = s->data[offset] & 0x7f;
  if (s->data[offset++] & 0x80) {
    if (offset >= s->size) return 0;
    length |= (size_t)s->data[offset++] << 7;
  }
  if (length > s->size - offset) return 0;
  *name = (const char *)s->data + offset; *size = length;
  return 1;
}
uint64_t xe_zar_size(const xe_zar *r, uint32_t node) {
  return xe_zar_type(r, node) == 1 ? node_size(node_ptr(r, node)) : 0;
}
uint32_t xe_zar_count(const xe_zar *r, uint32_t node) {
  return xe_zar_type(r, node) == 0 ? be32(node_ptr(r, node) + 8) : 0;
}
int xe_zar_entry_get(const xe_zar *r, uint32_t node, uint32_t index,
    xe_zar_entry *out) {
  uint32_t child;
  if (!out || index >= xe_zar_count(r, node)) return 0;
  child = be32(node_ptr(r, node) + 4) + index;
  if (child >= r->nodes || !name_get(r, child, &out->name, &out->name_size)) return 0;
  out->is_file = xe_zar_type(r, child);
  out->size = out->is_file ? xe_zar_size(r, child) : 0;
  return out->name_size != 0;
}
static int equal_name(const char *a, const char *b, size_t length) {
  size_t i;
  unsigned x, y;
  for (i = 0; i < length; ++i) {
    x = (unsigned char)a[i]; y = (unsigned char)b[i];
    if (x >= 'A' && x <= 'Z') x += 'a' - 'A';
    if (y >= 'A' && y <= 'Z') y += 'a' - 'A';
    if (x != y) return 0;
  }
  return 1;
}
uint32_t xe_zar_lookup(const xe_zar *r, const char *path, size_t length,
    int allow_file, int allow_directory) {
  uint32_t current = 0, first, count, i;
  size_t part, name_size;
  const char *name;
  int found, type;
  if (!r || (!path && length)) return XE_ZAR_INVALID;
  while (length) {
    while (length && (*path == '/' || *path == '\\')) { ++path; --length; }
    if (!length) break;
    if (xe_zar_type(r, current) != 0) return XE_ZAR_INVALID;
    for (part = 0; part < length && path[part] != '/' && path[part] != '\\'; ++part) {}
    first = be32(node_ptr(r, current) + 4); count = xe_zar_count(r, current);
    found = 0;
    for (i = 0; i < count; ++i) {
      if (name_get(r, first + i, &name, &name_size) && name_size == part &&
          equal_name(name, path, part)) { current = first + i; found = 1; break; }
    }
    if (!found) return XE_ZAR_INVALID;
    path += part; length -= part;
  }
  type = xe_zar_type(r, current);
  return (type == 1 ? allow_file : allow_directory) ? current : XE_ZAR_INVALID;
}
static int section_load(xe_zar *r, zar_section *s) {
  if (s->size > (size_t)-1) return 0;
  if (!s->size) { s->data = (const uint8_t *)""; return 1; }
  s->data = xe_file_view(r->file, s->offset, (size_t)s->size);
  if (s->data) return 1;
  s->owned = (uint8_t *)malloc((size_t)s->size);
  if (!s->owned || !xe_file_read(r->file, s->offset, s->owned, (size_t)s->size)) return 0;
  s->data = s->owned;
  return 1;
}
static int tree_validate(const xe_zar *r) {
  uint32_t *parents;
  uint32_t i, j, start, count, next;
  uint64_t offset, size, capacity = r->blocks * BLOCK_SIZE;
  const uint8_t *n;
  const char *name;
  size_t length;
  int good = 0;
  /* The addressable 16-byte node table already bounds this 4-byte array. */
  if (xe_zar_type(r, 0) != 0) return 0;
  parents = (uint32_t *)malloc((size_t)r->nodes * sizeof(*parents));
  if (!parents) return 0;
  for (i = 0; i < r->nodes; ++i) parents[i] = XE_ZAR_INVALID;
  parents[0] = 0;
  for (i = 0; i < r->nodes; ++i) {
    n = node_ptr(r, i);
    if (!name_get(r, i, &name, &length) || (i == 0 ? length != 0 : length == 0)) goto done;
    if (i && (memchr(name, '/', length) || memchr(name, '\\', length) || memchr(name, 0, length))) goto done;
    if (n[0] & 0x80) {
      offset = node_offset(n); size = node_size(n);
      if (offset > capacity || size > capacity - offset) goto done;
    } else {
      start = be32(n + 4); count = be32(n + 8);
      if (start > r->nodes || count > r->nodes - start) goto done;
      for (j = start; j < start + count; ++j) {
        if (parents[j] != XE_ZAR_INVALID) goto done;
        parents[j] = i;
      }
    }
  }
  /* Each node has exactly one parent, and every chain must reach root.
   * Temporary marking and path compression make this linear without recursion. */
  for (i = 1; i < r->nodes; ++i) if (parents[i] == XE_ZAR_INVALID) goto done;
  for (i = 1; i < r->nodes; ++i) {
    j = i;
    while (parents[j] != 0) {
      if (parents[j] & 0x80000000U) goto done;
      next = parents[j]; parents[j] |= 0x80000000U; j = next;
    }
    j = i;
    while (parents[j] & 0x80000000U) {
      next = parents[j] & 0x7fffffffU; parents[j] = 0; j = next;
    }
  }
  good = 1;
done:
  free(parents);
  return good;
}
xe_zar *xe_zar_open(const char *path) {
  uint8_t footer[144];
  xe_zar *r;
  uint64_t size, offset, length;
  unsigned i;
  r = (xe_zar *)calloc(1, sizeof(*r));
  if (!r) return NULL;
  r->file = xe_file_open(path);
  if (!r->file) goto fail;
  size = xe_file_size(r->file);
  if (size <= sizeof(footer) || !xe_file_read(r->file, size - sizeof(footer), footer, sizeof(footer))) goto fail;
  if (be32(footer + 140) != 0x169f52d6U || be32(footer + 136) != 0x61bf3a01U || be64(footer + 128) != size) goto fail;
  for (i = 0; i < 6; ++i) {
    offset = be64(footer + i * 16); length = be64(footer + i * 16 + 8);
    if (offset > size || length > size - offset) goto fail;
    if (i < 4) { r->sections[i].offset = offset; r->sections[i].size = length; }
  }
  if (!r->sections[1].size || r->sections[1].size > 0xffffffffU || r->sections[1].size % 40 ||
      r->sections[2].size > 0x7fffffffU || !r->sections[3].size ||
      r->sections[3].size > 0xffffffffU || r->sections[3].size % 16) goto fail;
  r->nodes = (uint32_t)(r->sections[3].size / 16);
  r->blocks = r->sections[1].size / 40 * 16;
  for (i = 1; i < 4; ++i) if (!section_load(r, &r->sections[i])) goto fail;
  if (size <= (size_t)-1) r->mapping = xe_file_view(r->file, 0, (size_t)size);
  if (!tree_validate(r)) goto fail;
  r->lock = slock_new();
  if (!r->lock) goto fail;
  r->first = 0; r->last = CACHE_COUNT - 1;
  for (i = 0; i < CACHE_COUNT; ++i) {
    r->cache[i].key = (uint64_t)-1;
    r->cache[i].prev = i ? i - 1 : NO_SLOT;
    r->cache[i].next = i + 1 < CACHE_COUNT ? i + 1 : NO_SLOT;
  }
  return r;
fail:
  xe_zar_close(r);
  return NULL;
}
void xe_zar_close(xe_zar *r) {
  unsigned i;
  if (!r) return;
  if (r->lock) slock_free(r->lock);
  rzstd_decoder_free(r->decoder);
  free(r->compressed); free(r->cache_data);
  for (i = 1; i < 4; ++i) free(r->sections[i].owned);
  xe_file_close(r->file);
  free(r);
}
static int block_location(const xe_zar *r, uint64_t block, uint64_t *offset, size_t *size) {
  const uint8_t *record;
  uint64_t relative;
  unsigned sub, i;
  uint32_t prefix = 0;
  if (block >= r->blocks) return 0;
  record = r->sections[1].data + (size_t)(block / 16) * 40;
  relative = be64(record); sub = (unsigned)(block % 16);
  for (i = 0; i < sub; ++i) {
    prefix += ((unsigned)record[8 + i * 2] << 8) + record[9 + i * 2] + 1;
  }
  *size = ((size_t)record[8 + sub * 2] << 8) + record[9 + sub * 2] + 1;
  /* At most sixteen 64 KiB lengths: prefix + size cannot overflow unsigned.
   * Check once before adding an untrusted 64-bit base or publishing a view. */
  if (relative > r->sections[0].size || prefix + *size > r->sections[0].size - relative) return 0;
  *offset = r->sections[0].offset + relative + prefix;
  return 1;
}
static unsigned hash_key(uint64_t key) {
  return (unsigned)((key ^ (key >> 32)) * 2654435761U) & (HASH_COUNT - 1);
}
static ZAR_NOINLINE const uint8_t *raw_range(const xe_zar *r, uint64_t position, uint64_t count) {
  const uint8_t *record;
  uint64_t block, location, first = 0, next = 0, remaining = count;
  size_t within, step, size;
  unsigned sub;
  if (!r->mapping) return NULL;
  block = position / BLOCK_SIZE; within = (size_t)(position % BLOCK_SIZE);
  while (remaining) {
    if (block >= r->blocks) return NULL;
    record = r->sections[1].data + (size_t)(block / 16) * 40;
    sub = (unsigned)(block % 16);
    /* Classify first: compressed cache hits need no prefix scan. */
    if (record[8 + sub * 2] != 255 || record[9 + sub * 2] != 255) return NULL;
    if (!block_location(r, block, &location, &size)) return NULL;
    if (remaining == count) first = location + within;
    else if (location != next) return NULL;
    step = BLOCK_SIZE - within;
    if (step > remaining) step = (size_t)remaining;
    remaining -= step; next = location + BLOCK_SIZE; within = 0; ++block;
  }
  /* Every complete physical block was checked inside the data section, and
   * adjacency proves the whole requested span lies in this owned mapping.
   * No decoder, cursor, cache state or publication is touched by this path. */
  return r->mapping + (size_t)first;
}
static void hash_remove(xe_zar *r, uint64_t key) {
  unsigned pos = hash_key(key), next, home, slot;
  while (r->hash[pos] && r->cache[r->hash[pos] - 1].key != key) pos = (pos + 1) & (HASH_COUNT - 1);
  if (!r->hash[pos]) return;
  next = (pos + 1) & (HASH_COUNT - 1);
  while ((slot = r->hash[next]) != 0) {
    home = hash_key(r->cache[slot - 1].key);
    if (((next - home) & (HASH_COUNT - 1)) >= ((next - pos) & (HASH_COUNT - 1))) {
      r->hash[pos] = slot; pos = next;
    }
    next = (next + 1) & (HASH_COUNT - 1);
  }
  r->hash[pos] = 0;
}
static void cache_touch(xe_zar *r, unsigned slot) {
  zar_cache *c = &r->cache[slot];
  if (r->last == slot) return;
  if (c->prev == NO_SLOT) r->first = c->next;
  else r->cache[c->prev].next = c->next;
  r->cache[c->next].prev = c->prev;
  r->cache[r->last].next = slot;
  c->prev = r->last; c->next = NO_SLOT; r->last = slot;
}
static ZAR_NOINLINE const uint8_t *block_load(xe_zar *r, uint64_t block) {
  unsigned pos, slot;
  uint64_t offset;
  size_t size, decoded;
  const uint8_t *input, *result;
  uint8_t *output;
  int status;
  if (!block_location(r, block, &offset, &size)) return NULL;
  input = xe_file_view(r->file, offset, size);
  if (!(size == BLOCK_SIZE && input) && !r->cache_data) {
    r->cache_data = (uint8_t *)malloc(CACHE_COUNT * BLOCK_SIZE);
    if (!r->cache_data) return NULL;
  }
  slot = r->first;
  if (r->cache[slot].key != (uint64_t)-1) hash_remove(r, r->cache[slot].key);
  r->cache[slot].key = (uint64_t)-1;
  r->cache[slot].view = NULL;
  if (size == BLOCK_SIZE && input) {
    /* The immutable file owner outlives all cached borrowed spans. */
    r->cache[slot].view = input;
    result = input;
    goto publish;
  }
  output = r->cache_data + slot * BLOCK_SIZE;
  if (size == BLOCK_SIZE) {
    if (!xe_file_read(r->file, offset, output, size)) return NULL;
  } else {
    if (!input) {
      if (!r->compressed) r->compressed = (uint8_t *)malloc(BLOCK_SIZE);
      if (!r->compressed || !xe_file_read(r->file, offset, r->compressed, size)) return NULL;
      input = r->compressed;
    }
    if (!r->decoder) r->decoder = rzstd_decoder_new();
    if (!r->decoder) return NULL;
    decoded = 0;
    status = rzstd_decode_reuse(r->decoder, output, BLOCK_SIZE, input, size, &decoded);
    if (status != RZSTD_PROCESS_END || decoded != BLOCK_SIZE) return NULL;
  }
  result = output;
publish:
  /* Only checksum-valid complete blocks become visible in the cache. */
  r->cache[slot].key = block;
  pos = hash_key(block);
  while (r->hash[pos]) pos = (pos + 1) & (HASH_COUNT - 1);
  r->hash[pos] = slot + 1;
  cache_touch(r, slot);
  return result;
}
static const uint8_t *block_get(xe_zar *r, uint64_t block) {
  unsigned pos, slot;
  pos = hash_key(block);
  while (r->hash[pos]) {
    slot = r->hash[pos] - 1;
    if (r->cache[slot].key == block) {
      cache_touch(r, slot);
      return r->cache[slot].view ? r->cache[slot].view : r->cache_data + slot * BLOCK_SIZE;
    }
    pos = (pos + 1) & (HASH_COUNT - 1);
  }
  return block_load(r, block);
}
uint64_t xe_zar_read(xe_zar *r, uint32_t node, uint64_t offset, uint64_t length, void *output) {
  uint64_t size, position, remaining, count;
  size_t within, step;
  uint8_t *out = (uint8_t *)output;
  const uint8_t *block;
  if (xe_zar_type(r, node) != 1) return 0;
  size = xe_zar_size(r, node);
  if (offset >= size) return 0;
  count = size - offset;
  if (count > length) count = length;
  if (count > (size_t)-1 || (!output && count)) return 0;
  if (!count) return 0;
  position = node_offset(node_ptr(r, node)) + offset;
  if (r->mapping) {
    uint64_t index = position / BLOCK_SIZE;
    const uint8_t *tag = r->sections[1].data + (size_t)(index / 16) * 40 +
        8 + (size_t)(index % 16) * 2;
    uint16_t encoded;
    /* The validated file extent bounds this tag. An all-ones length is
     * endian-independent; avoid calling the raw probe on compressed hits. */
    memcpy(&encoded, tag, sizeof(encoded));
    if (encoded == 0xffff) {
      block = raw_range(r, position, count);
      if (block) { memcpy(out, block, (size_t)count); return count; }
    }
  }
  remaining = count;
  slock_lock(r->lock);
  while (remaining) {
    within = (size_t)(position % BLOCK_SIZE);
    step = BLOCK_SIZE - within;
    if (step > remaining) step = (size_t)remaining;
    block = block_get(r, position / BLOCK_SIZE);
    if (!block) { slock_unlock(r->lock); return 0; }
    memcpy(out, block + within, step);
    out += step; position += step; remaining -= step;
  }
  slock_unlock(r->lock);
  return count;
}
#ifdef XE_ZAR_TEST
size_t xe_zar_test_cache_bytes(const xe_zar *r) {
  return r->cache_data ? CACHE_COUNT * BLOCK_SIZE : 0;
}
size_t xe_zar_test_metadata_bytes(const xe_zar *r) {
  size_t result = 0;
  unsigned i;
  for (i = 1; i < 4; ++i) if (r->sections[i].owned) result += (size_t)r->sections[i].size;
  return result;
}
#endif
