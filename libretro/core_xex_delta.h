/* Bounded borrowed delta packets. Source bytes remain immutable through apply;
 * destination/reference windows share one owned image and may overlap. */
#ifndef XE_CORE_XEX_DELTA_H
#define XE_CORE_XEX_DELTA_H
#include "core_xex_image.h"
typedef int (*xe_delta_decode)(void *, const uint8_t *, size_t,
    uint8_t *, size_t, uint32_t, const uint8_t *, size_t);
static INLINE unsigned xe_delta_u16(const uint8_t *p) {
  return ((unsigned)p[0] << 8) | p[1];
}
static INLINE int xe_delta_validate(const uint8_t *patch, size_t length,
    uint32_t window, size_t destination_size, size_t reference_size) {
  size_t offset = 0, old, target, unpacked, compressed;
  if (!patch && length) return 0;
  while (offset < length) {
    if (length - offset < 12) return 0;
    old = xe_xex_u32(patch + offset); target = xe_xex_u32(patch + offset + 4);
    unpacked = xe_delta_u16(patch + offset + 8);
    compressed = xe_delta_u16(patch + offset + 10);
    if (!old && !target && !unpacked && !compressed) return 1;
    if (target > destination_size || unpacked > destination_size - target) return 0;
    if (compressed && (old > reference_size || unpacked > reference_size - old)) return 0;
    if (compressed > 1) {
      if (compressed > length - offset - 12 || !window ||
          (window & (window - 1)) || unpacked > window) return 0;
      offset += compressed;
    }
    offset += 12;
  }
  return 1;
}
/* Preflight the entire packet stream before writing, not just each packet on
 * arrival. Decoder failures may leave a modified prefix; no transaction implied. */
static INLINE int xe_delta_execute(const uint8_t *patch, size_t length,
    uint32_t window, uint8_t *image, xe_delta_decode decode, void *opaque) {
  size_t offset = 0, old, target, unpacked, compressed;
  int result;
  while (offset < length) {
    old = xe_xex_u32(patch + offset); target = xe_xex_u32(patch + offset + 4);
    unpacked = xe_delta_u16(patch + offset + 8);
    compressed = xe_delta_u16(patch + offset + 10);
    if (!old && !target && !unpacked && !compressed) return 0;
    if (!compressed) {
      if (unpacked) memset(image + target, 0, unpacked);
    } else if (compressed == 1) {
      if (unpacked) memmove(image + target, image + old, unpacked);
    } else {
      result = decode(opaque, patch + offset + 12, compressed, image + target,
          unpacked, window, image + old, unpacked);
      if (result) return result;
      offset += compressed;
    }
    offset += 12;
  }
  return 0;
}
static INLINE int xe_delta_apply(const uint8_t *patch, size_t length,
    uint32_t window, uint8_t *image, size_t destination_size, size_t reference_size,
    xe_delta_decode decode, void *opaque) {
  if (!image || !decode || !xe_delta_validate(patch, length, window,
        destination_size, reference_size) || xe_xex_overlap(patch, length, image,
          destination_size > reference_size ? destination_size : reference_size)) return 1;
  return xe_delta_execute(patch, length, window, image, decode, opaque);
}
typedef struct xe_delta_chain {
  const uint8_t *input;
  size_t length, destination_size, reference_size;
  uint32_t first, window;
} xe_delta_chain;
/* Hash every delta block and validate every contained packet before image moves
 * or writes. No gather/registry allocation: both passes borrow the block spans. */
static INLINE int xe_delta_chain_validate(xe_delta_chain *plan, const uint8_t *input, size_t length,
    uint32_t first_size, const uint8_t first_hash[20], uint32_t window,
    size_t destination_size, size_t reference_size, xe_xex_hash hash, void *opaque) {
  uint8_t expected[20], digest[20];
  uint32_t size = first_size, next;
  size_t offset = 0;
  if (!plan || !hash || !first_hash || (!input && length)) return 0;
  memcpy(expected, first_hash, 20);
  while (size) {
    if (offset > length || size < 24 || size > length - offset) return 0;
    hash(opaque, input + offset, size, digest);
    if (memcmp(expected, digest, 20) || !xe_delta_validate(input + offset + 24,
          size - 24, window, destination_size, reference_size)) return 0;
    memcpy(expected, input + offset + 4, 20); next = xe_xex_u32(input + offset);
    offset += size; size = next;
  }
  plan->input = input; plan->length = length; plan->first = first_size;
  plan->window = window; plan->destination_size = destination_size;
  plan->reference_size = reference_size; return 1;
}
/* Call only after successful chain validation with identical immutable input. */
static INLINE int xe_delta_chain_apply(const xe_delta_chain *plan, uint8_t *image,
    xe_delta_decode decode, void *opaque) {
  uint32_t size, next;
  size_t offset = 0;
  int result;
  if (!plan || !image || !decode || xe_xex_overlap(plan->input, plan->length, image,
        plan->destination_size > plan->reference_size ? plan->destination_size : plan->reference_size)) return 1;
  size = plan->first;
  while (size) {
    next = xe_xex_u32(plan->input + offset);
    result = xe_delta_execute(plan->input + offset + 24, size - 24, plan->window,
        image, decode, opaque);
    if (result) return result;
    offset += size; size = next;
  }
  return 0;
}
#endif