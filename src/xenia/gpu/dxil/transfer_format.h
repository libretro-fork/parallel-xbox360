/* Native adapter for shared format construction. BSD; MSVC C89. */
#ifndef XE_DXIL_TRANSFER_FORMAT_H
#define XE_DXIL_TRANSFER_FORMAT_H
#include "guest_alu.h"
#include "libretro/core_transfer_format.h"
#ifdef __cplusplus
extern "C" {
#endif
int xe_dxil_transfer_format_init(xe_transfer_format_emitter *,XeDxilAlu *);
uintptr_t xe_dxil_transfer_format_emit(void *,enum xe_transfer_format_op,uintptr_t,uintptr_t,uintptr_t);
#ifdef __cplusplus
}
#endif
#endif
