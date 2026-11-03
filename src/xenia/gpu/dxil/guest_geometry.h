/* Native primitive expansion. MSVC C89; BSD license. */
#ifndef XE_GPU_DXIL_GUEST_GEOMETRY_H
#define XE_GPU_DXIL_GUEST_GEOMETRY_H
#include "shader_module.h"
typedef struct XeDxilGeometryOptions {
  unsigned type; /* point=1, rectangle=2, quad=3, line=4 */
  unsigned interpolators, clip_planes, cull_planes;
  unsigned vertex_kill_and, point_size, point_coordinates;
  unsigned point_diameter_word, point_radius_word;
} XeDxilGeometryOptions;
#ifdef __cplusplus
extern "C" {
#endif
int xe_dxil_build_geometry(const XeDxilGeometryOptions *options, XeDxilShaderBinary *binary);
XeDxilShaderBinary *xe_dxil_build_geometry_owned(const XeDxilGeometryOptions *options);
#ifdef __cplusplus
}
#endif
#endif
