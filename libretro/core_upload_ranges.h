/* GPU-command-thread scratch. The synchronous upload call borrows an exclusive
 * mutable span; native GPU commands never retain this CPU range metadata. */
#ifndef XE_CORE_UPLOAD_RANGES_H
#define XE_CORE_UPLOAD_RANGES_H
#include <stdint.h>
#include <retro_inline.h>
typedef struct xe_upload_range { uint32_t first, second; } xe_upload_range;
/* Visit input indices once in ascending order, with *written initially zero.
 * Keep the original records before the first hole untouched. Thereafter only
 * surviving records move, into already-consumed slots of the same owner span.
 * Returns whether a record moved (useful to measure metadata traffic). */
static INLINE int xe_upload_range_keep(xe_upload_range *ranges,
    uint32_t input, uint32_t *written) {
  uint32_t output = *written;
  if (output != input) ranges[output] = ranges[input];
  *written = output + 1;
  return output != input;
}
#endif
