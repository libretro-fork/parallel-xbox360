#ifndef XE_CORE_DECOMPRESS_H
#define XE_CORE_DECOMPRESS_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Decode a complete stream into the caller's exact-size destination. */
int xe_decompress_exact(void *output, size_t output_size,
    const void *input, size_t input_size, int window_bits);
/* Returns malloc-owned bytes only on success. The caller chooses a hard cap. */
int xe_decompress_alloc(uint8_t **output, size_t *output_size,
    const void *input, size_t input_size, int window_bits,
    size_t initial_capacity, size_t limit);
#ifdef __cplusplus
}
#endif
#endif
