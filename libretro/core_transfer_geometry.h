/* Compact transfer rectangle instances. BSD license. MSVC C89. */
#ifndef XE_CORE_TRANSFER_GEOMETRY_H
#define XE_CORE_TRANSFER_GEOMETRY_H
#include <stdint.h>
#include <string.h>
#include "core_spv_binary.h"
#include <retro_inline.h>
typedef struct xe_transfer_rectangle { float x0,y0,x1,y1; } xe_transfer_rectangle;
typedef char xe_transfer_rectangle_size[(sizeof(xe_transfer_rectangle)==16)?1:-1];
enum { XE_TRANSFER_RECTANGLE_VERTICES=6, XE_TRANSFER_RECTANGLE_BYTES=16,
 XE_TRANSFER_VK_X_MASK=0x2c, XE_TRANSFER_VK_Y_MASK=0x32,
 XE_TRANSFER_D3D_X_MASK=0x32, XE_TRANSFER_D3D_Y_MASK=0x2c };
static INLINE void xe_transfer_rectangle_store(float *out,float x0,float y0,float x1,float y1) {
 out[0]=x0;out[1]=y0;out[2]=x1;out[3]=y1;
}
static INLINE int xe_transfer_rectangle_bytes(uint32_t count,uint32_t *bytes) {
 if(!bytes || count>UINT32_MAX/XE_TRANSFER_RECTANGLE_BYTES)return 0;
 *bytes=count*XE_TRANSFER_RECTANGLE_BYTES;return 1;
}
#ifdef __cplusplus
extern "C" {
#endif
/* Initialized before gameplay. Caller owns/reuses words; failure publishes no
 * partial module. Attribute0 is float4 per instance; VertexIndex must be0..5. */
int xe_transfer_rectangle_spv(xe_spv_binary *out);
#ifdef __cplusplus
}
#endif
#endif
