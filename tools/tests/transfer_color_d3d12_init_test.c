/* Native driver initialization and lookup-only accounting. BSD; MSVC C89.
 * Component test only: this does not certify rendered pixels or frame time. */
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <initguid.h>
#include <dxgi1_4.h>
#include <stdio.h>
#include <string.h>
#include "libretro/core_transfer_color_d3d12.h"
#include "libretro/core_transfer_uint_d3d12.h"
#include "src/xenia/gpu/dxil/transfer_rectangle.h"
#include "transfer_test_adapter.h"
#ifdef XE_COLOR_D3D12_IMAGE_TEST
#include "transfer_color_d3d12_images.h"
#endif
static xe_transfer_color_d3d12_ops forward;
static unsigned emits,creates,releases,live,errors;
static size_t bytes,peak;
static double emit_time,create_time;
static double now(void)
{
 LARGE_INTEGER n,f;
 if(!QueryPerformanceCounter(&n) || !QueryPerformanceFrequency(&f))ExitProcess(2);
 return (double)n.QuadPart/(double)f.QuadPart;
}
static int emit(const XeDxilEdramTransferOptions *options,XeDxilShaderBinary *binary)
{
 double start=now();int result=forward.emit(options,binary);
 emit_time+=now()-start;++emits;
 if(!result)fprintf(stderr,"emit failed s%u d%u sn%u dn%u converted%u\n",options->source_format,
  options->destination_format,options->source_native,options->destination_native,options->value_convert);
 if(binary->data){++live;bytes+=binary->size;if(binary->size>peak)peak=binary->size;}
 return result;
}
static void free_shader(XeDxilShaderBinary *binary)
{
 if(binary->data){if(!live)++errors;else --live;}
 forward.free_shader(binary);
}
static HRESULT create(ID3D12Device *device,const D3D12_GRAPHICS_PIPELINE_STATE_DESC *desc,ID3D12PipelineState **out)
{
 double start=now();HRESULT result=forward.create_pipeline(device,desc,out);
 create_time+=now()-start;++creates;
 if(FAILED(result))fprintf(stderr,"create failed ordinal%u HRESULT%08lx\n",creates,(unsigned long)result);
 return result;
}
static void release(ID3D12PipelineState *pipeline)
{++releases;forward.release_pipeline(pipeline);}
int main(int argc,char **argv)
{
 IDXGIFactory4 *factory=NULL;IDXGIAdapter *adapter=NULL;ID3D12Device *device=NULL;
 ID3D12RootSignature *root=NULL;DXGI_ADAPTER_DESC info;
 D3D12_ROOT_SIGNATURE_DESC root_desc={0};D3D12_ROOT_PARAMETER parameters[2]={{0}};
 D3D12_DESCRIPTOR_RANGE range={0};D3D12_FEATURE_DATA_FORMAT_SUPPORT support={0};
 const xe_transfer_uint_d3d12_ops *native;
 xe_transfer_color_d3d12_bank bank={0};xe_transfer_color_d3d12_config config={0};
 xe_transfer_color_d3d12_ops ops={0};XeDxilShaderBinary vertex={0};
 ID3D12PipelineState *const *found;HRESULT status=E_FAIL;unsigned f,s,d,sn,dn,c,scaled,lookups=0,count;
 double start,total;int hardware=argc==2 && !strcmp(argv[1],"hardware"),result=1;
 if(argc>2 || (argc==2 && !hardware))return 2;
#define REQUIRE(x) do{if(!(x)){fprintf(stderr,"color init line%u status%08lx\n",(unsigned)__LINE__,(unsigned long)status);goto done;}}while(0)
#define HR(x) do{status=(x);REQUIRE(SUCCEEDED(status));}while(0)
 HR(CreateDXGIFactory1(&IID_IDXGIFactory4,(void**)&factory));
 HR(transfer_test_adapter(factory,hardware,&adapter));HR(IDXGIAdapter_GetDesc(adapter,&info));
 HR(D3D12CreateDevice((IUnknown*)adapter,D3D_FEATURE_LEVEL_11_0,&IID_ID3D12Device,(void**)&device));
 printf("adapter hardware%d vendor%u device%u revision%u\n",hardware,info.VendorId,info.DeviceId,info.Revision);
 range.RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV;range.NumDescriptors=1;
 parameters[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
 parameters[0].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
 parameters[0].Constants.ShaderRegister=XE_DXIL_PUSH_CONSTANT_REGISTER;
 parameters[0].Constants.RegisterSpace=XE_DXIL_PUSH_CONSTANT_SPACE;parameters[0].Constants.Num32BitValues=4;
 parameters[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
 parameters[1].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
 parameters[1].DescriptorTable.NumDescriptorRanges=1;parameters[1].DescriptorTable.pDescriptorRanges=&range;
 root_desc.NumParameters=2;root_desc.pParameters=parameters;
 root_desc.Flags=D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
 native=xe_transfer_uint_d3d12_native_ops();HR(native->create_root(device,&root_desc,&root));
 REQUIRE(xe_dxil_build_transfer_rectangle(&vertex));
 REQUIRE(xe_transfer_color_d3d12_native_ops(&forward));ops=forward;
 ops.emit=emit;ops.free_shader=free_shader;ops.create_pipeline=create;ops.release_pipeline=release;
 config.device=device;config.root=root;config.vertex=vertex.data;config.vertex_bytes=vertex.size;
 for(f=0;f<8;++f){
  support.Format=xe_transfer_color_d3d12_view(f);
  HR(ID3D12Device_CheckFeatureSupport(device,D3D12_FEATURE_FORMAT_SUPPORT,&support,sizeof(support)));
  if((support.Support1&(D3D12_FORMAT_SUPPORT1_TEXTURE2D|D3D12_FORMAT_SUPPORT1_SHADER_LOAD|
      D3D12_FORMAT_SUPPORT1_RENDER_TARGET))==(D3D12_FORMAT_SUPPORT1_TEXTURE2D|
      D3D12_FORMAT_SUPPORT1_SHADER_LOAD|D3D12_FORMAT_SUPPORT1_RENDER_TARGET))config.enabled_views|=1u<<f;
 }
 REQUIRE(config.enabled_views==255);
 for(scaled=0;scaled<2;++scaled){
  config.scale_x=scaled?3:1;config.scale_y=scaled?5:1;
  emits=creates=releases=live=errors=0;bytes=peak=0;emit_time=create_time=0;
  start=now();REQUIRE(xe_transfer_color_d3d12_init(&bank,&config,&ops));total=now()-start;
  count=scaled?180:45;REQUIRE(emits==count && creates==count && !live && !errors && !releases);
  for(s=0;s<8;++s)for(d=0;d<8;++d)for(sn=0;sn<2;++sn)for(dn=0;dn<2;++dn)for(c=0;c<2;++c){
   static const unsigned formats[8]={0,1,2,3,4,5,14,15};
   int valid=c?(s==3 && d==0):(s!=d && !(s>=4 && d>=4));
   found=xe_transfer_color_d3d12_get(&bank,formats[s],formats[d],dn,sn,c);
   REQUIRE((found!=NULL)==valid);++lookups;
  }
  REQUIRE(emits==count && creates==count && !live && !errors && !releases);
#ifdef XE_COLOR_D3D12_IMAGE_TEST
  REQUIRE(color_images(device,&config,&bank));
  REQUIRE(emits==count && creates==count && !live && !errors && !releases);
#endif
  REQUIRE(xe_transfer_color_d3d12_close(&bank,&ops));REQUIRE(releases==count);
  printf("bank scaled%u PSOs%u final_bytes%zu peak_blob%zu total_seconds%.9f emit_seconds%.9f driver_seconds%.9f\n",
   scaled,count,bytes,peak,total,emit_time,create_time);
 }
 printf("PASS: %u native lookup cases; no lazy emission/creation, all PSOs released\n",lookups);result=0;
done:
 if(bank.device)xe_transfer_color_d3d12_close(&bank,&ops);
 xe_dxil_shader_binary_free(&vertex);
 if(root)ID3D12RootSignature_Release(root);if(device)ID3D12Device_Release(device);
 if(adapter)IDXGIAdapter_Release(adapter);if(factory)IDXGIFactory4_Release(factory);
 return result;
}
