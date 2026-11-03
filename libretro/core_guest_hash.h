#ifndef XE_CORE_GUEST_HASH_H
#define XE_CORE_GUEST_HASH_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define XE_GUEST_SHA1_SIZE 88
#define XE_GUEST_HMAC_SHA1_SIZE 176
/* SHA-1 uses BE32 byte count, five BE32 words and a 64-byte tail.
 * Final retains count and publishes chaining words plus the last padded block.
 * The streamed HMAC API consumes at most 64 key bytes. */
int xe_guest_sha1_init(uint8_t *state);
int xe_guest_sha1_update(uint8_t *state, const uint8_t *input, size_t size);
int xe_guest_sha1_final(uint8_t *state, uint8_t *out, size_t out_size);
int xe_guest_hmac_sha1_init(uint8_t *state, const uint8_t *key, size_t key_size);
int xe_guest_hmac_sha1_final(uint8_t *state, uint8_t *out, size_t out_size);
#define XE_GUEST_SHA256_SIZE 100
#define XE_GUEST_MD5_SIZE 88
#define XE_GUEST_SHA512_SIZE 200
/* MD5: BE64 byte count, four BE32 words in D,C,B,A order, 64 bytes.
 * SHA-512: BE64 byte count, eight BE64 words, 128 bytes.
 * Final writes output before publishing the digest in the guest buffer;
 * count, chaining words and unused buffer bytes retain their old values. */
int xe_guest_md5_init(uint8_t *state);
int xe_guest_md5_update(uint8_t *state, const uint8_t *input, size_t size);
int xe_guest_md5_final(uint8_t *state, uint8_t *out, size_t out_size);
int xe_guest_sha512_init(uint8_t *state);
int xe_guest_sha512_update(uint8_t *state, const uint8_t *input, size_t size);
int xe_guest_sha512_final(uint8_t *state, uint8_t *out, size_t out_size);
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
