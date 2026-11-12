/* Owned prepared numeric identity PSOs. BSD; MSVC C89. */
#ifndef XE_CORE_TRANSFER_IDENTITY_D3D12_H
#define XE_CORE_TRANSFER_IDENTITY_D3D12_H
#include <d3d12.h>
#include "src/xenia/gpu/dxil/transfer_identity.h"
#ifdef __cplusplus
extern "C" {
#endif
enum {XE_TRANSFER_IDENTITY_D3D12_PIPELINES=144};
typedef struct xe_transfer_identity_d3d12_ops {
 HRESULT (*create_root)(ID3D12Device*,const D3D12_ROOT_SIGNATURE_DESC*,ID3D12RootSignature**);
 HRESULT (*create_pipeline)(ID3D12Device*,const D3D12_GRAPHICS_PIPELINE_STATE_DESC*,ID3D12PipelineState**);
 void (*release_root)(ID3D12RootSignature*);
 void (*release_pipeline)(ID3D12PipelineState*);
 int (*emit)(const xe_transfer_identity_options*,const xe_transfer_address_key*,XeDxilShaderBinary*);
 void (*free_shader)(XeDxilShaderBinary*);
} xe_transfer_identity_d3d12_ops;
typedef struct xe_transfer_identity_d3d12_config {
 ID3D12Device *device;
 const void *vertex;size_t vertex_bytes;
 unsigned native2,scale_x,scale_y;
 /* Per typed view, bits0/1/2 select supported guest sample enums. Derive
  * from actual device format/load/MSAA support and global native2 policy.
  * Zero omits an unused/unsupported view, including disabled gamma16. */
 unsigned sample_mask[XE_TRANSFER_IDENTITY_VIEWS];
} xe_transfer_identity_d3d12_config;
typedef struct xe_transfer_identity_d3d12_bank {
 ID3D12Device *device;
 ID3D12RootSignature *root;
 ID3D12PipelineState *pipeline[XE_TRANSFER_IDENTITY_D3D12_PIPELINES];
 unsigned sample_mask[XE_TRANSFER_IDENTITY_VIEWS];
 unsigned scale_classes,native2,ready;
} xe_transfer_identity_d3d12_bank;
/* Fill caller-owned cold operation metadata. No retained pointer or allocation. */
int xe_transfer_identity_d3d12_native_ops(xe_transfer_identity_d3d12_ops *out);
/* Zero-initialize bank. Config/ops/vertex bytes borrowed through return.
 * Complete ready publication only; every partial acquisition unwinds. Each
 * shape's float module is borrowed by four typed PSOs before being released. */
int xe_transfer_identity_d3d12_init(xe_transfer_identity_d3d12_bank *bank,
 const xe_transfer_identity_d3d12_config *config,const xe_transfer_identity_d3d12_ops *ops);
/* Requires the owner's existing GPU-completion retirement before close. */
int xe_transfer_identity_d3d12_close(xe_transfer_identity_d3d12_bank *bank,
 const xe_transfer_identity_d3d12_ops *ops);
/* Lookup only, including sample-support checks. No creation/queueing/wait. */
ID3D12PipelineState *xe_transfer_identity_d3d12_get(const xe_transfer_identity_d3d12_bank *bank,
 unsigned view,unsigned dest_msaa,unsigned source_msaa,unsigned dest_native,unsigned source_native);
#ifdef __cplusplus
}
#endif
#endif
