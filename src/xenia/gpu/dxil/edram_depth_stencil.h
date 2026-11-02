/* Native EDRAM depth/stencil operations. MSVC C89; BSD license. */
#ifndef XE_GPU_DXIL_EDRAM_DEPTH_STENCIL_H
#define XE_GPU_DXIL_EDRAM_DEPTH_STENCIL_H
#include "guest_alu.h"
typedef struct XeDxilDepthStencilState {
  XeDxilValue depth_enabled, depth_write, depth_function;
  XeDxilValue stencil_enabled, stencil_function, reference, read_mask, write_mask;
  XeDxilValue stencil_fail, depth_fail, depth_pass;
} XeDxilDepthStencilState;
typedef struct XeDxilDepthStencilResult {
  XeDxilValue packed, passed, stencil_failed, depth_failed, changed;
} XeDxilDepthStencilResult;
#ifdef __cplusplus
extern "C" {
#endif
/* Packed EDRAM words contain depth in bits 8..31 and stencil in bits 0..7.
 * Functions and stencil operations use the Xenos 0..7 encodings. Inputs are
 * i32; result predicates are i1. This emits register operations only: callers
 * own ordered reads/writes, sample coverage and early/late write policy. */
int xe_dxil_edram_depth_stencil(XeDxilAlu *alu, const XeDxilDepthStencilState *state,
  XeDxilValue previous, XeDxilValue depth, XeDxilDepthStencilResult *result);
#ifdef __cplusplus
}
#endif
#endif
