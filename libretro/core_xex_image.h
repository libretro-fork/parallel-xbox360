/* Borrowed XEX wire preflight, CBC and checked forward chunk compaction. */
#ifndef XE_CORE_XEX_IMAGE_H
#define XE_CORE_XEX_IMAGE_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <retro_inline.h>
static INLINE uint32_t xe_xex_u32(const uint8_t *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static INLINE int xe_xex_overlap(const void *a, size_t a_size,
    const void *b, size_t b_size) {
  uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
  if (!a_size || !b_size) return 0;
  return x <= y ? y - x < a_size : x - y < b_size;
}
static INLINE int xe_xex_header_valid(const void *data, size_t length,
    uint32_t *header_size) {
  const uint8_t *b = (const uint8_t *)data;
  uint32_t magic, size, count, security, fixed, i, key, offset, bytes, pages;
  if (!b || length < 24) return 0;
  magic = xe_xex_u32(b); size = xe_xex_u32(b + 8);
  count = xe_xex_u32(b + 20); security = xe_xex_u32(b + 16);
  if (magic == 0x58455832U) fixed = 0x184;
  else if (magic == 0x58455831U) fixed = 0x168;
  else if (magic == 0x58455825U) fixed = 0x154;
  else return 0;
  if (size < 24 || size > length || count > (size - 24) / 8 ||
      (security & 3) || security > size || fixed > size - security) return 0;
  pages = xe_xex_u32(b + security + fixed - 4);
  if (pages > (size - security - fixed) / 24) return 0;
  for (i = 0; i < count; ++i) {
    key = xe_xex_u32(b + 24 + (size_t)i * 8);
    if ((key & 255) <= 1) continue;
    offset = xe_xex_u32(b + 28 + (size_t)i * 8);
    if ((offset & 3) || offset > size) return 0;
    if ((key & 255) == 255) {
      if (size - offset < 4) return 0;
      bytes = xe_xex_u32(b + offset);
      if (bytes < 4) return 0;
    } else bytes = (key & 255) * 4;
    if (bytes > size - offset) return 0;
    if (key == 0x000003ffU) {
      /* Both mode fields are 16-bit big endian. */
      if (bytes < 8 || b[offset + 4] || b[offset + 5] > 1 || b[offset + 6] ||
          b[offset + 7] > 3) return 0;
      if (b[offset + 7] == 1 && ((bytes - 8) & 7)) return 0;
      if (b[offset + 7] >= 2 && bytes < 36) return 0;
    }
  }
  if (header_size) *header_size = size;
  return 1;
}
typedef void (*xe_xex_block_decrypt)(void *, const uint8_t *, uint8_t *);
static INLINE int xe_xex_page_bytes(const uint8_t *records, uint32_t count,
    uint32_t page_size, uint32_t limit, uint32_t *result) {
  uint32_t total = 0, pages, i;
  if (!result || !page_size || (!records && count)) return 0;
  for (i = 0; i < count; ++i) {
    pages = xe_xex_u32(records + (size_t)i * 24) >> 4;
    if (pages > (limit - total) / page_size) return 0;
    total += pages * page_size;
  }
  *result = total; return 1;
}
typedef int (*xe_xex_copy_span)(void *, const uint8_t *, uint8_t *, size_t);
/* Basic-compression data is borrowed; holes and final padding alone are zeroed.
 * The entire layout is checked before the first destination/decrypt operation. */
static INLINE int xe_xex_basic_copy(const uint8_t *info, size_t info_size,
    const uint8_t *input, size_t length, uint8_t *output, size_t capacity,
    xe_xex_copy_span decrypt, void *context) {
  size_t i, read = 0, written = 0, data, zero;
  int encrypted;
  uintptr_t a = (uintptr_t)input, b = (uintptr_t)output, c = (uintptr_t)info;
  if (!info || info_size < 8 || ((info_size - 8) & 7) ||
      !input || !output || !capacity ||
      (a <= b ? b - a < length : a - b < capacity) ||
      (c <= b ? b - c < info_size : c - b < capacity)) return 0;
  encrypted = info[5] == 1;
  if (info[4] || info[5] > 1 || info[6] || info[7] != 1 ||
      (encrypted && !decrypt)) return 0;
  for (i = 8; i < info_size; i += 8) {
    data = xe_xex_u32(info + i); zero = xe_xex_u32(info + i + 4);
    if (data > length - read || (encrypted && (data & 15)) ||
        data > capacity - written || zero > capacity - written - data) return 0;
    read += data; written += data + zero;
  }
  read = written = 0;
  for (i = 8; i < info_size; i += 8) {
    data = xe_xex_u32(info + i); zero = xe_xex_u32(info + i + 4);
    if (data) {
      if (encrypted) {
        if (!decrypt(context, input + read, output + written, data)) return 0;
      } else memcpy(output + written, input + read, data);
    }
    if (zero) memset(output + written + data, 0, zero);
    read += data; written += data + zero;
  }
  if (capacity != written) memset(output + written, 0, capacity - written);
  return 1;
}
/* Exact in-place or disjoint spans only. Save ciphertext before output, so CBC
 * chaining remains correct during in-place decode. IV continues across calls. */
static INLINE int xe_xex_cbc(void *context, xe_xex_block_decrypt decrypt,
    uint8_t iv[16], const uint8_t *source, size_t size,
    uint8_t *output, size_t capacity) {
  uint8_t cipher[16];
  size_t offset;
  unsigned i;
  uintptr_t a = (uintptr_t)source, b = (uintptr_t)output;
  if (!decrypt || !iv || (!source && size) || (!output && size) ||
      (size & 15) || size > capacity ||
      (a != b && (a < b ? b - a < size : a - b < size))) return 0;
  for (offset = 0; offset < size; offset += 16) {
    memcpy(cipher, source + offset, 16);
    decrypt(context, cipher, output + offset);
    for (i = 0; i < 16; ++i) output[offset + i] ^= iv[i];
    memcpy(iv, cipher, 16);
  }
  return 1;
}
typedef void (*xe_xex_hash)(void *, const uint8_t *, size_t, uint8_t *);
/* Input/output can be identical: chunk bytes only move behind the read cursor.
 * Next-block metadata is saved before compacting over it. Failed output is not
 * published (out_size=0), but its private workspace can be partially modified. */
static INLINE int xe_xex_deblock(const uint8_t *input, size_t length,
    uint32_t block_size, const uint8_t first_hash[20], xe_xex_hash hash,
    void *context, uint8_t *output, size_t capacity, size_t *out_size) {
  uint8_t expected[20], digest[20], next_hash[20];
  size_t offset = 0, written = 0, cursor, end, chunk;
  uint32_t next_size;
  uintptr_t a = (uintptr_t)input, b = (uintptr_t)output;
  if (out_size) *out_size = 0;
  if (!input || !output || !hash || !first_hash || !out_size || !block_size ||
      (a != b && (a < b ? b - a < length : a - b < capacity))) return 0;
  memcpy(expected, first_hash, 20);
  while (block_size) {
    if (offset > length || block_size < 26 || block_size > length - offset) return 0;
    end = offset + block_size;
    hash(context, input + offset, block_size, digest);
    if (memcmp(digest, expected, 20)) return 0;
    next_size = xe_xex_u32(input + offset);
    memcpy(next_hash, input + offset + 4, 20);
    cursor = offset + 24;
    for (;;) {
      if (end - cursor < 2) return 0;
      chunk = ((size_t)input[cursor] << 8) | input[cursor + 1]; cursor += 2;
      if (!chunk) break;
      if (chunk > end - cursor || written > capacity || chunk > capacity - written) return 0;
      memmove(output + written, input + cursor, chunk);
      written += chunk; cursor += chunk;
    }
    offset = end; block_size = next_size; memcpy(expected, next_hash, 20);
  }
  *out_size = written; return 1;
}
#endif
