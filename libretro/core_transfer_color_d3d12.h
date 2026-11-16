/* Prepared single-sample native color conversions. BSD; MSVC C89. */
#ifndef XE_CORE_TRANSFER_COLOR_D3D12_H
#define XE_CORE_TRANSFER_COLOR_D3D12_H
#include <d3d12.h>
#include "core_transfer_color.h"
#include "src/xenia/gpu/dxil/edram_transfer.h"
#ifdef __cplusplus
extern "C" {
#endif
enum {XE_TRANSFER_COLOR_D3D12_PAIRS=45,XE_TRANSFER_COLOR_D3D12_PIPELINES=180};
typedef struct xe_transfer_color_d3d12_ops {
 HRESULT (*create_pipeline)(ID3D12Device*,const D3D12_GRAPHICS_PIPELINE_STATE_DESC*,ID3D12PipelineState**);
 void (*release_pipeline)(ID3D12PipelineState*);
 int (*emit)(const XeDxilEdramTransferOptions*,XeDxilShaderBinary*);
 void (*free_shader)(XeDxilShaderBinary*);
} xe_transfer_color_d3d12_ops;
typedef struct xe_transfer_color_d3d12_config {
 ID3D12Device *device;
 ID3D12RootSignature *root;
 const void *vertex;size_t vertex_bytes;
 unsigned scale_x,scale_y,enabled_views,source_space;
} xe_transfer_color_d3d12_config;
typedef struct xe_transfer_color_d3d12_bank {
 ID3D12Device *device;
 unsigned scale_classes,enabled_views,ready;
 ID3D12PipelineState *pipeline[XE_TRANSFER_COLOR_D3D12_PIPELINES];
} xe_transfer_color_d3d12_bank;
/* Actual typed view for a normalized class0..7, UNKNOWN otherwise. */
DXGI_FORMAT xe_transfer_color_d3d12_view(unsigned);
/* Fill cold callback metadata; no retained callback-table owner or allocation. */
int xe_transfer_color_d3d12_native_ops(xe_transfer_color_d3d12_ops*);
/* Zero-initialize bank. Device/root/vertex are borrowed, never AddRef'd here.
 * Root and input code stay alive through initialization; the renderer retains
 * its root for binding until GPU uses complete. Native PSO creation consumes
 * final bytecode directly. No lazy creation, map or request queue in get().
 * Failure unwinds every shader and acquired PSO, including a failed callback
 * returning a non-null PSO. Startup cost must be measured separately. */
int xe_transfer_color_d3d12_init(xe_transfer_color_d3d12_bank*,
 const xe_transfer_color_d3d12_config*,const xe_transfer_color_d3d12_ops*);
/* After the owner's existing CPU/GPU completion protocol, before root teardown. */
int xe_transfer_color_d3d12_close(xe_transfer_color_d3d12_bank*,const xe_transfer_color_d3d12_ops*);
/* Actual guest formats, not DXGI enums. Single sample/color-to-color/target0
 * only. value_convert=1 is the existing 7e3 -> plain8888 numeric policy only;
 * no other value conversion or gamma policy is implied by an alias. */
ID3D12PipelineState *const *xe_transfer_color_d3d12_get(const xe_transfer_color_d3d12_bank*,
 unsigned source_format,unsigned dest_format,unsigned dest_native,unsigned source_native,unsigned value_convert);
#ifdef __cplusplus
}
#endif
#endif
