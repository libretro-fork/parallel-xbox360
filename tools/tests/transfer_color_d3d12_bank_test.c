/* Fixed-bank ownership/coverage and borrowed-bytecode tests. BSD; MSVC C89.
 * Mock callbacks are not a driver/GPU correctness or timing result. */
#include "libretro/core_transfer_color_d3d12.h"
#include "libretro/core_transfer_uint_d3d12.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
typedef union token {
 void *alignment;
 struct {unsigned live,source,dest,dn,sn,converted;} data;
} token;
static token objects[180];
static unsigned emitted,created,released,shader_freed,shader_live,fail_mode,fail_at,errors;
static unsigned char shader[4];static void *device_marker,*root_marker,*vertex_marker;
static XeDxilEdramTransferOptions active;
static const DXGI_FORMAT views[8]={DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R16G16B16A16_UNORM,
 DXGI_FORMAT_R10G10B10A2_UNORM,DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_R16G16_UINT,
 DXGI_FORMAT_R16G16B16A16_UINT,DXGI_FORMAT_R32_UINT,DXGI_FORMAT_R32G32_UINT};
#define CHECK(x) do{if(!(x)){fprintf(stderr,"Color bank line%u mode%u at%u\n",(unsigned)__LINE__,fail_mode,fail_at);return 1;}}while(0)
static unsigned view_class(unsigned format)
{
 static const unsigned map[16]={0,1,2,3,4,5,4,5,99,99,2,99,3,99,6,7};
 return format<16?map[format]:99;
}
static int should_fail(unsigned mode){return fail_mode==mode && emitted==fail_at;}
int xe_dxil_build_edram_transfer(const XeDxilEdramTransferOptions *options,XeDxilShaderBinary *out)
{
 ++emitted;if(shader_live)++errors;active=*options;
 out->data=shader;out->size=sizeof(shader);shader_live=1;
 if(should_fail(1))return 0;
 if(should_fail(2))out->size=0;
 if(should_fail(3))out->size=SIZE_MAX;
 return 1;
}
void xe_dxil_shader_binary_free(XeDxilShaderBinary *out)
{
 if(out->data){if(out->data!=shader || !shader_live)++errors;shader_live=0;++shader_freed;}
 out->data=NULL;out->size=0;
}
static HRESULT create(ID3D12Device *device,const D3D12_GRAPHICS_PIPELINE_STATE_DESC *desc,ID3D12PipelineState **out)
{
 unsigned s=view_class(active.source_format),d=view_class(active.destination_format);token *value;
 if(!shader_live || desc->PS.pShaderBytecode!=shader || desc->PS.BytecodeLength!=sizeof(shader) ||
  device!=(ID3D12Device*)&device_marker || desc->pRootSignature!=(ID3D12RootSignature*)&root_marker ||
  desc->VS.pShaderBytecode!=&vertex_marker || desc->VS.BytecodeLength!=sizeof(vertex_marker) ||
  s>=8 || d>=8 || desc->NumRenderTargets!=1 || desc->RTVFormats[0]!=views[d] ||
  desc->SampleDesc.Count!=1 || desc->SampleDesc.Quality || desc->SampleMask!=UINT32_MAX ||
  desc->BlendState.RenderTarget[0].BlendEnable || desc->BlendState.RenderTarget[0].RenderTargetWriteMask!=15 ||
  desc->DepthStencilState.DepthEnable || desc->DepthStencilState.StencilEnable ||
  desc->InputLayout.NumElements!=1 || !desc->InputLayout.pInputElementDescs ||
  strcmp(desc->InputLayout.pInputElementDescs[0].SemanticName,"POSITION") ||
  desc->InputLayout.pInputElementDescs[0].Format!=DXGI_FORMAT_R32G32B32A32_FLOAT ||
  desc->InputLayout.pInputElementDescs[0].InputSlotClass!=D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA ||
  desc->InputLayout.pInputElementDescs[0].InstanceDataStepRate!=1 ||
  desc->PrimitiveTopologyType!=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE ||
  desc->RasterizerState.FillMode!=D3D12_FILL_MODE_SOLID || desc->RasterizerState.CullMode!=D3D12_CULL_MODE_NONE ||
  !desc->RasterizerState.DepthClipEnable || active.mode!=1 || active.source_msaa || active.destination_msaa ||
  active.source_uint!=(unsigned)(s>=4) || active.destination_uint!=(unsigned)(d>=4) || active.source_space!=7)++errors;
 if(should_fail(4)){*out=NULL;return E_FAIL;}
 if(should_fail(6)){*out=NULL;return S_OK;}
 if(created>=180){++errors;return E_FAIL;}
 value=objects+created++;value->data.live=1;value->data.source=s;value->data.dest=d;
 value->data.dn=active.destination_native;value->data.sn=active.source_native;value->data.converted=active.value_convert;
 *out=(ID3D12PipelineState*)value;return should_fail(5)?E_FAIL:S_OK;
}
static void release(ID3D12PipelineState *object)
{
 token *value=(token*)object;
 if((uintptr_t)value<(uintptr_t)objects ||
  (uintptr_t)value>=(uintptr_t)(objects+180) ||
  ((uintptr_t)value-(uintptr_t)objects)%sizeof(*objects) || !value->data.live)++errors;
 else value->data.live=0;
 ++released;
}
const xe_transfer_uint_d3d12_ops *xe_transfer_uint_d3d12_native_ops(void)
{
 static const xe_transfer_uint_d3d12_ops ops={NULL,create,NULL,release,NULL,NULL};return &ops;
}
static void reset(void)
{memset(objects,0,sizeof(objects));emitted=created=released=shader_freed=shader_live=errors=0;fail_mode=fail_at=0;}
static unsigned expected_count(unsigned mask)
{
 unsigned s,d,result=0;
 for(s=0;s<8;++s)for(d=0;d<8;++d)if((mask&(1u<<s)) && (mask&(1u<<d))){
  if(s!=d && !(s>=4 && d>=4))++result;
  if(s==3 && d==0)++result;
 }
 return result;
}
int main(void)
{
 static const unsigned masks[6]={0,1,3,15,16,255};
 xe_transfer_color_d3d12_bank bank={0},before;xe_transfer_color_d3d12_config config={0},bad;
 xe_transfer_color_d3d12_ops ops={0},bad_ops;ID3D12PipelineState *const *found;
 unsigned m,scaled,s,d,sn,dn,convert,sc,dc,valid,i,mode,at,total,lookups=0;
 config.device=(ID3D12Device*)&device_marker;config.root=(ID3D12RootSignature*)&root_marker;
 config.vertex=&vertex_marker;config.vertex_bytes=sizeof(vertex_marker);config.source_space=7;
 CHECK(xe_transfer_color_d3d12_native_ops(&ops));CHECK(!xe_transfer_color_d3d12_native_ops(NULL));
 for(i=0;i<8;++i)CHECK(xe_transfer_color_d3d12_view(i)==views[i]);
 CHECK(xe_transfer_color_d3d12_view(8)==DXGI_FORMAT_UNKNOWN);
 CHECK(!xe_transfer_color_class(0,NULL));CHECK(!xe_transfer_color_class(18,&i));
 CHECK(!xe_transfer_color_pair(0,1,NULL));CHECK(!xe_transfer_color_pair(8,0,&i));
 CHECK(!xe_transfer_color_pair(0,8,&i));
 for(scaled=0;scaled<2;++scaled)for(m=0;m<6;++m){
  reset();config.scale_x=scaled?3:1;config.scale_y=scaled?5:1;config.enabled_views=masks[m];
  CHECK(xe_transfer_color_d3d12_init(&bank,&config,&ops));total=expected_count(masks[m])*(scaled?4:1);
  CHECK(bank.ready && created==total && emitted==total && shader_freed==total && !shader_live && !errors);
  before=bank;CHECK(!xe_transfer_color_d3d12_init(&bank,&config,&ops));CHECK(!memcmp(&bank,&before,sizeof(bank)));
  for(s=0;s<18;++s)for(d=0;d<18;++d)for(sn=0;sn<3;++sn)for(dn=0;dn<3;++dn)for(convert=0;convert<3;++convert){
   sc=view_class(s);dc=view_class(d);valid=sc<8 && dc<8 && sn<2 && dn<2 && convert<2;
   if(valid)valid=(masks[m]&(1u<<sc)) && (masks[m]&(1u<<dc)) &&
    (convert?(sc==3 && dc==0):(sc!=dc && !(sc>=4 && dc>=4)));
   found=xe_transfer_color_d3d12_get(&bank,s,d,dn,sn,convert);CHECK((found!=NULL)==(valid!=0));
   if(found){token *value=(token*)*found;CHECK(value->data.live && value->data.source==sc && value->data.dest==dc &&
    value->data.dn==(scaled?dn:0) && value->data.sn==(scaled?sn:0) && value->data.converted==convert);}
   ++lookups;
  }
  CHECK(created==total && emitted==total && shader_freed==total && !released && !errors);
  bad_ops=ops;bad_ops.release_pipeline=NULL;CHECK(!xe_transfer_color_d3d12_close(&bank,&bad_ops));CHECK(!memcmp(&bank,&before,sizeof(bank)));
  CHECK(xe_transfer_color_d3d12_close(&bank,&ops));CHECK(released==created && !bank.device && !bank.ready && !errors);
  for(i=0;i<180;++i)CHECK(!objects[i].data.live && !bank.pipeline[i]);
 }
 config.scale_x=4;config.scale_y=4;config.enabled_views=255;
 for(mode=1;mode<=6;++mode)for(at=1;at<=180;++at){
  reset();fail_mode=mode;fail_at=at;CHECK(!xe_transfer_color_d3d12_init(&bank,&config,&ops));
  CHECK(!bank.device && !bank.ready && !shader_live && !errors && released==created && shader_freed==emitted);
  for(i=0;i<180;++i)CHECK(!objects[i].data.live && !bank.pipeline[i]);
 }
 reset();bad=config;bad.scale_x=0;CHECK(!xe_transfer_color_d3d12_init(&bank,&bad,&ops));
 bad=config;bad.scale_y=UINT32_MAX;CHECK(!xe_transfer_color_d3d12_init(&bank,&bad,&ops));
 bad=config;bad.enabled_views=256;CHECK(!xe_transfer_color_d3d12_init(&bank,&bad,&ops));
 bad=config;bad.vertex_bytes=0;CHECK(!xe_transfer_color_d3d12_init(&bank,&bad,&ops));
 bad_ops=ops;bad_ops.free_shader=NULL;CHECK(!xe_transfer_color_d3d12_init(&bank,&config,&bad_ops));
 CHECK(!created && !emitted && !released && !shader_live && !errors);
 printf("PASS: %u lookup-only cases; 1080 partial failures, borrowed final bytecode and balanced PSO/shader ownership (mock callbacks only)\n",lookups);
 return 0;
}
