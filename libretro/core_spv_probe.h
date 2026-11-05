/* Raw compute probe: caller-owned final words, no builder/IR/heap dependency. */
#ifndef XE_CORE_SPV_PROBE_H
#define XE_CORE_SPV_PROBE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { XE_SPV_COMPUTE_PROBE_WORDS = 39 };
size_t xe_spv_compute_probe(uint32_t *words, size_t capacity,
      uint32_t version, uint32_t generator);
#ifdef __cplusplus
}
#endif
#endif
