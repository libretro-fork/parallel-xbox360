#ifndef XE_TEST_TRANSFER_UINT_D3D12_IMAGE_H
#define XE_TEST_TRANSFER_UINT_D3D12_IMAGE_H
/* Actual raw unsigned texture/fragment transfers through WARP. BSD; C89. */
#include "libretro/core_transfer_uint.h"
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <initguid.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static INLINE void *read_binary(const char *path,size_t *bytes) {
 FILE *f=fopen(path,"rb");long n;void *p;
 if(!f)return NULL;
 if(fseek(f,0,SEEK_END) || (n=ftell(f))<=0 || n>1048576 || fseek(f,0,SEEK_SET)){fclose(f);return NULL;}
 p=malloc((size_t)n);if(!p || fread(p,1,(size_t)n,f)!=(size_t)n){free(p);fclose(f);return NULL;}
 fclose(f);*bytes=(size_t)n;return p;
}
static INLINE DXGI_FORMAT format(unsigned f) {
#ifdef XE_TRANSFER_IMAGE_IDENTITY_MS
 static const DXGI_FORMAT views[4]={DXGI_FORMAT_R8G8B8A8_UINT,DXGI_FORMAT_R16G16B16A16_UINT,
  DXGI_FORMAT_R10G10B10A2_UINT,DXGI_FORMAT_R16G16B16A16_UINT};
 return views[f];
#elif defined(XE_TRANSFER_IMAGE_NUMERIC)
 static const DXGI_FORMAT views[4]={DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R16G16B16A16_UNORM,
  DXGI_FORMAT_R10G10B10A2_UNORM,DXGI_FORMAT_R16G16B16A16_FLOAT};
 return views[f];
#else
 return f==5 || f==7?DXGI_FORMAT_R16G16B16A16_UINT:f==14?DXGI_FORMAT_R32_UINT:f==15?DXGI_FORMAT_R32G32_UINT:DXGI_FORMAT_R16G16_UINT;
#endif
}
static INLINE HRESULT buffer(ID3D12Device *device,D3D12_HEAP_TYPE type,UINT64 bytes,ID3D12Resource **out) {
 D3D12_HEAP_PROPERTIES heap;D3D12_RESOURCE_DESC desc;
 memset(&heap,0,sizeof(heap));heap.Type=type;memset(&desc,0,sizeof(desc));
 desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=bytes;desc.Height=desc.DepthOrArraySize=desc.MipLevels=1;
 desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
 return ID3D12Device_CreateCommittedResource(device,&heap,D3D12_HEAP_FLAG_NONE,&desc,
  type==D3D12_HEAP_TYPE_UPLOAD?D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_COPY_DEST,NULL,&IID_ID3D12Resource,(void**)out);
}
static INLINE HRESULT texture_samples(ID3D12Device *device,unsigned f,unsigned w,unsigned h,int target,unsigned samples,ID3D12Resource **out) {
 D3D12_HEAP_PROPERTIES heap;D3D12_RESOURCE_DESC desc;
 memset(&heap,0,sizeof(heap));heap.Type=D3D12_HEAP_TYPE_DEFAULT;memset(&desc,0,sizeof(desc));
 desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=w;desc.Height=h;desc.DepthOrArraySize=desc.MipLevels=1;
 desc.SampleDesc.Count=samples;desc.Format=format(f);desc.Flags=target?D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET:D3D12_RESOURCE_FLAG_NONE;
#ifdef XE_TRANSFER_IMAGE_IDENTITY_MS
 /* Oracle aliases only. The separate single-sample tests retain actual typed
  * resources. Typed MSAA mode also uses separate normal resources for the
  * transfer; these typeless textures remain the seed/probe oracle only. */
 desc.Format=f==0?DXGI_FORMAT_R8G8B8A8_TYPELESS:f==2?DXGI_FORMAT_R10G10B10A2_TYPELESS:DXGI_FORMAT_R16G16B16A16_TYPELESS;
#endif
 return ID3D12Device_CreateCommittedResource(device,&heap,D3D12_HEAP_FLAG_NONE,&desc,
  target?D3D12_RESOURCE_STATE_RENDER_TARGET:D3D12_RESOURCE_STATE_COPY_DEST,NULL,&IID_ID3D12Resource,(void**)out);
}
static INLINE void transition(ID3D12GraphicsCommandList *list,ID3D12Resource *resource,D3D12_RESOURCE_STATES old,D3D12_RESOURCE_STATES next) {
 D3D12_RESOURCE_BARRIER b;memset(&b,0,sizeof(b));b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
 b.Transition.pResource=resource;b.Transition.StateBefore=old;b.Transition.StateAfter=next;b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
 ID3D12GraphicsCommandList_ResourceBarrier(list,1,&b);
}

static INLINE HRESULT texture(ID3D12Device *device,unsigned f,unsigned w,unsigned h,int target,ID3D12Resource **out) {
 return texture_samples(device,f,w,h,target,1,out);
}
#endif
