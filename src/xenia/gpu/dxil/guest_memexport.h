/* Native guest memory exports. MSVC C89; BSD license. */
#ifndef XE_GPU_DXIL_GUEST_MEMEXPORT_H
#define XE_GPU_DXIL_GUEST_MEMEXPORT_H
#include "guest_resources.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Format numbers are xenos::ColorFormat (not render-target formats).
 * Returns element bytes, or zero for an unsupported format/error. Unused
 * float32 lanes remain available to the guest's 64/128-bit endian swizzles. */
unsigned xe_dxil_memexport_pack(XeDxilAlu *alu, unsigned format,
  XeDxilValue format_info, XeDxilValue source[4], XeDxilValue packed[4]);
/* Emit at the current block, updating io->current_block. Address and data are
 * borrowed float SSA values; written is a runtime five-bit eM mask. allowed is
 * an optional invocation condition. The caller owns dirty-mask lifetime. */
int xe_dxil_memexport_store(XeDxilAlu *alu, XeDxilAluIo *io,
  XeDxilValue shared_uav, XeDxilValue address[4], XeDxilValue data[5][4],
  unsigned potential_mask, XeDxilValue written, XeDxilValue allowed);
#ifdef __cplusplus
}
#endif
#endif
