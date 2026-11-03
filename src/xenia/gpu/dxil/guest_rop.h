/* Native guest render-backend epilogue. MSVC C89; BSD license. */
#ifndef XE_GPU_DXIL_GUEST_ROP_H
#define XE_GPU_DXIL_GUEST_ROP_H
#include "rov_depth.h"
#include "guest_alu.h"
typedef struct XeDxilGuestRop XeDxilGuestRop;
typedef struct XeDxilGuestRopOptions {
  XeDxilRovDepthOptions depth;
  unsigned color_mask, formats[4], no_blending, early_depth;
  unsigned base_word, blend_word, keep_word, clamp_word, blend_constant_word;
} XeDxilGuestRopOptions;
#ifdef __cplusplus
extern "C" {
#endif
/* Create before guest control flow, in the entry block. Declares ordered
 * EDRAM u2 and query-counter u1 in space 0, using the supplied system CBV. */
XeDxilGuestRop *xe_dxil_guest_rop_create(XeDxilShaderModule *shader,
  XeDxilValue system, const XeDxilGuestRopOptions *options);
void xe_dxil_guest_rop_destroy(XeDxilGuestRop *rop);
/* Raw host coverage uses samples 0/3 for emulated guest 2x. Commit coverage
 * excludes killed samples. Early tests retain failure counts before kills,
 * while writes and pass counts use commit coverage. NULL depth uses raster
 * depth plus per-face offset; explicit guest depth applies to all samples.
 * Colors include guest exponent bias, but no output gamma conversion. */
int xe_dxil_guest_rop_emit(XeDxilGuestRop *rop, XeDxilValue position[3],
  XeDxilValue front, XeDxilValue flags, XeDxilValue coverage, XeDxilValue commit_coverage, XeDxilValue depth,
  XeDxilValue depth_dx, XeDxilValue depth_dy, XeDxilValue written,
  XeDxilValue colors[4][4], unsigned *current_block);
#ifdef __cplusplus
}
#endif
#endif
