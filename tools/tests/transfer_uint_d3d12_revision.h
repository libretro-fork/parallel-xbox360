/* Test-only cross-revision D3D12 ABI. Owned objects stay in their producer DLL. */
#ifndef XE_TEST_TRANSFER_D3D12_REVISION_H
#define XE_TEST_TRANSFER_D3D12_REVISION_H
#include <d3d12.h>
#include <stddef.h>
#include <stdint.h>
enum {XE_TEST_D3D12_ABI=1,XE_TEST_D3D12_LEGACY=1,XE_TEST_D3D12_PREPARED=2,XE_TEST_D3D12_MAX_PACKET=112};
/* The harness serializes open calls. Validation and vertex spans expire on
 * return. Root/pipeline results are borrowed until close after GPU completion.
 * Packet destinations must be uint32_t-aligned and exactly packet_bytes long. */
typedef struct xe_test_d3d12_config {
 ID3D12Device *device;const void *vertex;size_t vertex_bytes;
 unsigned native2,scale_x,scale_y;
 int (*validate)(void*,void*,size_t);void *validation_context;
} xe_test_d3d12_config;
typedef struct xe_test_d3d12_key {
 uint32_t source_format,dest_format,source_msaa,dest_msaa;
 uint32_t source_pitch,dest_pitch;int32_t delta;
 uint32_t source_native,dest_native;
} xe_test_d3d12_key;
typedef struct xe_test_d3d12_api {
 uint32_t version,bytes,config_bytes,key_bytes,packet_bytes,kind,constants_root,image_root;
 void *(*open)(const xe_test_d3d12_config*);
 ID3D12RootSignature *(*root)(void*);
 ID3D12PipelineState *(*pipeline)(void*,const xe_test_d3d12_key*);
 int (*packet)(void*,const xe_test_d3d12_key*,void*,size_t);
 void (*close)(void*);
} xe_test_d3d12_api;
typedef const xe_test_d3d12_api *(*xe_test_d3d12_lookup)(unsigned);
/* Input headers must be validated before invoking any producer callbacks. */
static int xe_test_d3d12_api_valid(const xe_test_d3d12_api *api) {
 return api && api->version==XE_TEST_D3D12_ABI && api->bytes==sizeof(*api) &&
  api->config_bytes==sizeof(xe_test_d3d12_config) && api->key_bytes==sizeof(xe_test_d3d12_key) &&
  ((api->kind==XE_TEST_D3D12_LEGACY && api->packet_bytes==4 && api->constants_root==0 && api->image_root==1) ||
   (api->kind==XE_TEST_D3D12_PREPARED && api->packet_bytes==XE_TEST_D3D12_MAX_PACKET && api->constants_root==1 && api->image_root==0)) &&
  api->open && api->root && api->pipeline && api->packet && api->close;
}
#endif
