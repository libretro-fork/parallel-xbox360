/* Native adapter for shared transfer addressing. BSD; MSVC C89. */
#ifndef XE_DXIL_TRANSFER_SAMPLE_H
#define XE_DXIL_TRANSFER_SAMPLE_H
#include "guest_alu.h"
#include "libretro/core_transfer_sample.h"
struct dxil_func;
typedef struct XeDxilTransferSample {
 XeDxilAlu *alu;
 const struct dxil_func *multiply;
} XeDxilTransferSample;
#ifdef __cplusplus
extern "C" {
#endif
int xe_dxil_transfer_sample_init(XeDxilTransferSample *,XeDxilAlu *);
uintptr_t xe_dxil_transfer_sample_emit(void *,enum xe_transfer_sample_op,
 uintptr_t,uintptr_t,uintptr_t);
#ifdef __cplusplus
}
#endif
#endif
