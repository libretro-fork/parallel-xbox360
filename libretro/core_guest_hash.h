#ifndef XE_CORE_GUEST_HASH_H
#define XE_CORE_GUEST_HASH_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define XE_GUEST_SHA256_SIZE 100
/* Guest layout: BE32 byte count, eight BE32 words, 64 buffer bytes.
 * State may be unaligned. Input is consumed before publishing state. */
int xe_guest_sha256_init(uint8_t *state);
int xe_guest_sha256_update(uint8_t *state, const uint8_t *input, size_t size);
/* Preserve guest finalization: count/words stay, digest enters buffer[0..31].
 * Output is written first, including when it aliases the guest state. */
int xe_guest_sha256_final(uint8_t *state, uint8_t *out, size_t out_size);
#ifdef __cplusplus
}
#endif
#endif
