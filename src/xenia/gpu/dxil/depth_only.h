/* Native host-depth pixel shaders. MSVC C89; BSD license. */
#ifndef XE_GPU_DXIL_DEPTH_ONLY_H
#define XE_GPU_DXIL_DEPTH_ONLY_H
#include "shader_module.h"
typedef struct XeDxilDepthOnlyOptions {
  unsigned depth_mode; /* 0: unchanged, 1: float24 truncation, 2: rounding. */
  unsigned count_coverage;
  unsigned system_constants_bytes, counter_index_word, counter_stride_bytes;
} XeDxilDepthOnlyOptions;
#ifdef __cplusplus
extern "C" {
#endif
int xe_dxil_build_depth_only(const XeDxilDepthOnlyOptions *options, XeDxilShaderBinary *binary);
/* Set storage must be zero-initialized. A failed build releases all partial
 * results. The free operation is safe after failure and on repeated calls. */
int xe_dxil_build_depth_only_set(const XeDxilDepthOnlyOptions *base,
  unsigned float24, unsigned coverage, XeDxilShaderBinary binaries[3][2]);
void xe_dxil_depth_only_set_free(XeDxilShaderBinary binaries[3][2]);
#ifdef __cplusplus
}
#endif
#endif
