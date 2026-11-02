/* Native EDRAM format operations. MSVC C89; BSD license. */
#ifndef XE_GPU_DXIL_EDRAM_FORMAT_H
#define XE_GPU_DXIL_EDRAM_FORMAT_H
#include "guest_alu.h"
#ifdef __cplusplus
extern "C" {
#endif
XeDxilValue xe_dxil_pack_float7e3(XeDxilAlu *alu, XeDxilValue value);
XeDxilValue xe_dxil_unpack_float7e3(XeDxilAlu *alu, XeDxilValue value);
/* Depth input is pre-clamped to [0,2). remap handles host [0,1) values
 * representing guest [0,2). Packed values contain the low 24 depth bits. */
XeDxilValue xe_dxil_pack_depth20e4(XeDxilAlu *alu, XeDxilValue value, int round_even, int remap);
XeDxilValue xe_dxil_unpack_depth20e4(XeDxilAlu *alu, XeDxilValue value, int remap);
/* Guest piecewise gamma conversions for pre-saturated float values. */
XeDxilValue xe_dxil_gamma_encode(XeDxilAlu *alu, XeDxilValue linear);
XeDxilValue xe_dxil_gamma_decode(XeDxilAlu *alu, XeDxilValue gamma);
/* Format numbers follow xenos::ColorRenderTargetFormat. Integer-view formats
 * preserve all bits, including half-float NaNs and signed fixed-point minima.
 * Return the number of packed dwords (one or two), or zero on failure. */
int xe_dxil_pack_edram_color(XeDxilAlu *alu, unsigned format, int source_uint,
                               XeDxilValue source[4], XeDxilValue packed[2]);
int xe_dxil_unpack_edram_color(XeDxilAlu *alu, unsigned format, int destination_uint,
                                 XeDxilValue packed[2], XeDxilValue destination[4]);
#ifdef __cplusplus
}
#endif
#endif
