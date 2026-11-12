/* Xenos vertex-fetch dword addressing. MSVC C89. */
#ifndef XE_CORE_VERTEX_ADDRESS_H
#define XE_CORE_VERTEX_ADDRESS_H
#include <stdint.h>
#include <retro_inline.h>

/* The instruction offset is signed, but GPU integer addition wraps modulo
 * 2^32. Convert before addition; converting a signed overflow afterwards is UB.
 * Range checks and physical mirror translation remain the caller's job. */
static INLINE uint32_t xe_vertex_fetch_address(uint32_t base, int32_t offset)
{
   return base + (uint32_t)offset;
}
#endif
