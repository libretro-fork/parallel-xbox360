/* Shared guest-stage D3D12 linkage. MSVC C89; BSD license.
 * Rows must survive absent/unused varyings and independent stage compilation.
 * TEXCOORD0..15 carry packed guest interpolators, TEXCOORD16 may carry points. */
#ifndef XE_GPU_DXIL_STAGE_LAYOUT_H
#define XE_GPU_DXIL_STAGE_LAYOUT_H
#define XE_DXIL_GUEST_POSITION_ROW 17u
#define XE_DXIL_GUEST_DISTANCE_ROW 18u
#define XE_DXIL_GUEST_FRONT_FACE_ROW 20u
#endif
