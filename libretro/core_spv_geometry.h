/* Xenos geometry expansion bounds and exact entry-point reservation. */
#ifndef XE_CORE_SPV_GEOMETRY_H
#define XE_CORE_SPV_GEOMETRY_H
#include <stddef.h>
#include <string.h>
#include <retro_inline.h>
enum { XE_SPV_GEOMETRY_INTERPOLATORS = 16, XE_SPV_GEOMETRY_CLIP_PLANES = 6 };
static INLINE int xe_spv_geometry_name(char *out, size_t capacity,
      int output, unsigned index)
{
   static const char in_prefix[] = "xe_in_interpolator_";
   static const char out_prefix[] = "xe_out_interpolator_";
   const char *prefix = output ? out_prefix : in_prefix;
   size_t length = output ? sizeof(out_prefix) - 1 : sizeof(in_prefix) - 1;
   size_t digits = index >= 10 ? 2 : 1;
   if (!out || index >= XE_SPV_GEOMETRY_INTERPOLATORS ||
       capacity < length + digits + 1) return 0;
   memcpy(out, prefix, length);
   if (digits == 2) out[length++] = '1';
   out[length++] = (char)('0' + index % 10);
   out[length] = 0;
   return 1;
}
static INLINE int xe_spv_geometry_plan(unsigned type, unsigned interpolators,
      unsigned clip_planes, int point_size, int point_coordinates,
      int list_all_globals, size_t *interfaces)
{
   if (!interfaces) return 0;
   *interfaces = 0;
   if (type < 1 || type > 4 || interpolators > XE_SPV_GEOMETRY_INTERPOLATORS ||
       clip_planes > XE_SPV_GEOMETRY_CLIP_PLANES) return 0;
   *interfaces = (size_t)interpolators * 2 + 2 + (point_size != 0) +
         (point_coordinates != 0) +
         ((type == 1 || type == 4) && list_all_globals);
   return 1;
}
#endif
