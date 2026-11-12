/* Initialization-owned integer D3D12 transfer pipelines. BSD; MSVC C89. */
#ifndef XE_CORE_TRANSFER_UINT_D3D12_H
#define XE_CORE_TRANSFER_UINT_D3D12_H
#include <d3d12.h>
#include "src/xenia/gpu/dxil/transfer_uint.h"
#ifdef __cplusplus
extern "C" {
#endif
enum {XE_TRANSFER_UINT_D3D12_PIPELINES=144};
typedef struct xe_transfer_uint_d3d12_ops {
 HRESULT (*create_root)(ID3D12Device*,const D3D12_ROOT_SIGNATURE_DESC*,ID3D12RootSignature**);
 HRESULT (*create_pipeline)(ID3D12Device*,const D3D12_GRAPHICS_PIPELINE_STATE_DESC*,ID3D12PipelineState**);
 void (*release_root)(ID3D12RootSignature*);
 void (*release_pipeline)(ID3D12PipelineState*);
 int (*emit)(const xe_transfer_uint_options*,const xe_transfer_address_key*,unsigned,XeDxilShaderBinary*);
 void (*free_shader)(XeDxilShaderBinary*);
} xe_transfer_uint_d3d12_ops;
typedef struct xe_transfer_uint_d3d12_config {
 ID3D12Device *device;
 const void *vertex;size_t vertex_bytes;
 unsigned native2,scale_x,scale_y;
} xe_transfer_uint_d3d12_config;
typedef struct xe_transfer_uint_d3d12_bank {
 ID3D12Device *device;
 ID3D12RootSignature *root;
 ID3D12PipelineState *pipeline[XE_TRANSFER_UINT_D3D12_PIPELINES];
 unsigned scale_classes,native2,ready;
} xe_transfer_uint_d3d12_bank;
/* Native synchronous creation callbacks, borrowed for initialization/teardown. */
const xe_transfer_uint_d3d12_ops *xe_transfer_uint_d3d12_native_ops(void);
/* Zero-initialize bank. Device is borrowed until close; vertex bytes are borrowed through return.
 * Only ready complete banks are published. Every failed creation closes all
 * previously acquired objects. No GPU waits, logging or frame-time allocation. */
int xe_transfer_uint_d3d12_init(xe_transfer_uint_d3d12_bank *bank,
 const xe_transfer_uint_d3d12_config *config,const xe_transfer_uint_d3d12_ops *ops);
/* Caller has completed GPU uses through its existing ownership/retirement path.
 * Missing release callbacks retain ownership and return0. */
int xe_transfer_uint_d3d12_close(xe_transfer_uint_d3d12_bank *bank,const xe_transfer_uint_d3d12_ops *ops);
/* Actual unsigned ownership-view format, sample enums0/1/2, native flags0/1.
 * Returns only existing handles. No shader emission, admission or creation. */
ID3D12PipelineState *xe_transfer_uint_d3d12_get(const xe_transfer_uint_d3d12_bank *bank,
 unsigned format,unsigned dest_msaa,unsigned source_msaa,unsigned dest_native,unsigned source_native);
#ifdef __cplusplus
}
#endif
#endif
