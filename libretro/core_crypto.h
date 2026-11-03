#ifndef XE_CORE_CRYPTO_H
#define XE_CORE_CRYPTO_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Inputs are borrowed synchronously. Up to 20 output bytes are written.
 * Output may alias an input. Invalid nonempty spans leave output untouched. */
int xe_hmac_sha1_3(const uint8_t *key, size_t key_size,
    const uint8_t *a, size_t a_size, const uint8_t *b, size_t b_size,
    const uint8_t *c, size_t c_size, uint8_t *out, size_t out_size);
/* Account-file RC4 uses the first 16 key bytes. Exact in-place is supported;
 * partial input/output overlap, short keys and short output are rejected. */
int xe_rc4_16(const uint8_t *key, size_t key_size, const uint8_t *input,
    size_t size, uint8_t *out, size_t out_size);
int xe_md5_3(const uint8_t *a, size_t a_size, const uint8_t *b, size_t b_size,
    const uint8_t *c, size_t c_size, uint8_t *out, size_t out_size);
#ifdef __cplusplus
}
#endif
#endif
