/* Fixed metadata for the 19 memory-export format branches, not guest data.
 * Callers generate active-register IDs directly in each reserved case. */
#ifndef XE_CORE_SPV_EXPORT_CASES_H
#define XE_CORE_SPV_EXPORT_CASES_H
#include <stddef.h>
#include <stdint.h>
#include <retro_inline.h>
enum { XE_SPV_EXPORT_REGISTERS = 5, XE_SPV_EXPORT_FORMAT_CASES = 19 };
typedef struct xe_spv_export_case {
  uint32_t packed[XE_SPV_EXPORT_REGISTERS];
  uint32_t element_bytes_log2;
  uint32_t phi_parent;
} xe_spv_export_case;
typedef struct xe_spv_export_cases {
  xe_spv_export_case data[XE_SPV_EXPORT_FORMAT_CASES];
  uint32_t count;
} xe_spv_export_cases;

/* Reset count to zero before each use. Reservation leaves unselected packed
 * lanes untouched; only active lanes are read. Capture phi_parent AFTER the body. */
static INLINE xe_spv_export_case *xe_spv_export_case_begin(
    xe_spv_export_cases *cases, uint32_t element_bytes_log2) {
  xe_spv_export_case *result;
  if (cases->count >= XE_SPV_EXPORT_FORMAT_CASES || element_bytes_log2 > 4)
    return NULL;
  result = &cases->data[cases->count++];
  result->element_bytes_log2 = element_bytes_log2;
  result->phi_parent = 0;
  return result;
}
#endif
