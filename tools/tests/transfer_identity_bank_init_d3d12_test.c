/* Full single-bank native initialization accounting. Test-only; BSD; MSVC C89.
 * Device/VS setup and process telemetry are outside constructor timing.
 * Driver internal allocation sizes are not observable through these APIs. */
#define main identity_reference_unused_main
#include "transfer_identity_msaa_reference_d3d12_test.c"
#undef main
#include <psapi.h>
static xe_transfer_identity_d3d12_ops init_forward;
static struct {
 unsigned roots,pipelines,emits,root_live,pipeline_live,blob_live,failed;
 size_t emitted_bytes,peak_blob,pipeline_input_bytes;
 double root_seconds,pipeline_seconds,emit_seconds;
} init_profile;
static double init_now(void) {
 LARGE_INTEGER tick,frequency;
 if(!QueryPerformanceCounter(&tick) || !QueryPerformanceFrequency(&frequency))exit(2);
 return (double)tick.QuadPart/(double)frequency.QuadPart;
}
static HRESULT init_root(ID3D12Device *device,const D3D12_ROOT_SIGNATURE_DESC *desc,ID3D12RootSignature **out) {
 double before=init_now();HRESULT result=init_forward.create_root(device,desc,out);
 init_profile.root_seconds+=init_now()-before;++init_profile.roots;
 if(SUCCEEDED(result) && *out)++init_profile.root_live;return result;
}
static HRESULT init_pipeline(ID3D12Device *device,const D3D12_GRAPHICS_PIPELINE_STATE_DESC *desc,ID3D12PipelineState **out) {
 double before=init_now();HRESULT result=init_forward.create_pipeline(device,desc,out);
 init_profile.pipeline_seconds+=init_now()-before;++init_profile.pipelines;
 init_profile.pipeline_input_bytes+=desc->PS.BytecodeLength;
 if(SUCCEEDED(result) && *out)++init_profile.pipeline_live;return result;
}
static void init_release_root(ID3D12RootSignature *root) {
 if(root){if(!init_profile.root_live)++init_profile.failed;else --init_profile.root_live;}
 init_forward.release_root(root);
}
static void init_release_pipeline(ID3D12PipelineState *pipeline) {
 if(pipeline){if(!init_profile.pipeline_live)++init_profile.failed;else --init_profile.pipeline_live;}
 init_forward.release_pipeline(pipeline);
}
static int init_emit(const xe_transfer_identity_options *options,const xe_transfer_address_key *key,XeDxilShaderBinary *out) {
 double before=init_now();int result=init_forward.emit(options,key,out);
 init_profile.emit_seconds+=init_now()-before;++init_profile.emits;
 if(out->data){++init_profile.blob_live;init_profile.emitted_bytes+=out->size;if(out->size>init_profile.peak_blob)init_profile.peak_blob=out->size;}
 return result;
}
static void init_free(XeDxilShaderBinary *out) {
 if(out->data){if(!init_profile.blob_live)++init_profile.failed;else --init_profile.blob_live;}
 init_forward.free_shader(out);
}
static int init_unsigned(const char *text,unsigned limit,unsigned *out) {
 unsigned n=0;
 if(!*text)return 0;
 while(*text){if(*text<'0' || *text>'9' || n>limit/10)return 0;n=n*10+(unsigned)(*text++-'0');if(n>limit)return 0;}
 *out=n;return 1;
}
static int init_memory(PROCESS_MEMORY_COUNTERS_EX *memory) {
 memset(memory,0,sizeof(*memory));memory->cb=sizeof(*memory);
 return GetProcessMemoryInfo(GetCurrentProcess(),(PROCESS_MEMORY_COUNTERS*)memory,sizeof(*memory))!=0;
}
#include "transfer_identity_first_draw_d3d12.h"
int main(int argc,char **argv) {
 IDXGIFactory4 *factory=NULL;IDXGIAdapter *adapter=NULL;ID3D12Device *device=NULL;
 xe_transfer_identity_d3d12_bank ready={0};xe_transfer_identity_d3d12_config config={0};
 xe_transfer_identity_d3d12_ops ops={0};D3D12_FEATURE_DATA_FORMAT_SUPPORT support;
 D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS quality;DXGI_ADAPTER_DESC adapter_desc;
 identity_first_draw first={0};
 PROCESS_MEMORY_COUNTERS_EX before,after,closed;void *vertex=NULL;size_t vertex_bytes=0;
 unsigned f,m,physical,native2=1,gamma,expected=0,scale_classes,lookups=0,n,d,s,a,b;
 ID3D12PipelineState *found;double started,elapsed,query_seconds;HRESULT status=E_FAIL;
 char path[4096];int hardware=getenv("MSAA_HARDWARE")!=NULL,debug=getenv("IDENTITY_MSAA_D3D12_DEBUG")!=NULL;
 if(argc!=5 || strlen(argv[1])>2048 || !init_unsigned(argv[2],16,&config.scale_x) ||
  !init_unsigned(argv[3],16,&config.scale_y) || !config.scale_x || !config.scale_y ||
  !init_unsigned(argv[4],1,&gamma))return 1;
 sprintf(path,"%s-vertex.dxil",argv[1]);vertex=read_binary(path,&vertex_bytes);if(!vertex)goto done;
#define INIT_OK(expression) do {status=(expression);if(FAILED(status)){fprintf(stderr,"Bank init failure line%u %08lx\n",(unsigned)__LINE__,(unsigned long)status);goto done;}}while(0)
 INIT_OK(identity_debug_enable(debug));INIT_OK(CreateDXGIFactory1(&IID_IDXGIFactory4,(void**)&factory));
 INIT_OK(transfer_test_adapter(factory,hardware,&adapter));INIT_OK(IDXGIAdapter_GetDesc(adapter,&adapter_desc));
 INIT_OK(D3D12CreateDevice((IUnknown*)adapter,D3D_FEATURE_LEVEL_11_0,&IID_ID3D12Device,(void**)&device));
 started=init_now();
 for(f=0;f<4;++f) {
  if(f==1 && !gamma)continue;
  memset(&quality,0,sizeof(quality));quality.Format=numeric_format(f);quality.SampleCount=2;
  INIT_OK(ID3D12Device_CheckFeatureSupport(device,D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,&quality,sizeof(quality)));
  if(!quality.NumQualityLevels)native2=0;
 }
 config.native2=native2;config.device=device;config.vertex=vertex;config.vertex_bytes=vertex_bytes;
 for(f=0;f<4;++f) {
  if(f==1 && !gamma)continue;
  memset(&support,0,sizeof(support));support.Format=numeric_format(f);
  INIT_OK(ID3D12Device_CheckFeatureSupport(device,D3D12_FEATURE_FORMAT_SUPPORT,&support,sizeof(support)));
  if((support.Support1&(D3D12_FORMAT_SUPPORT1_TEXTURE2D|D3D12_FORMAT_SUPPORT1_SHADER_LOAD|D3D12_FORMAT_SUPPORT1_RENDER_TARGET))!=
    (D3D12_FORMAT_SUPPORT1_TEXTURE2D|D3D12_FORMAT_SUPPORT1_SHADER_LOAD|D3D12_FORMAT_SUPPORT1_RENDER_TARGET))continue;
  physical=1;
  if((support.Support1&(D3D12_FORMAT_SUPPORT1_MULTISAMPLE_LOAD|D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RENDERTARGET))==
    (D3D12_FORMAT_SUPPORT1_MULTISAMPLE_LOAD|D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RENDERTARGET)) {
   for(m=2;m<=4;m*=2){memset(&quality,0,sizeof(quality));quality.Format=numeric_format(f);quality.SampleCount=m;
    INIT_OK(ID3D12Device_CheckFeatureSupport(device,D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,&quality,sizeof(quality)));if(quality.NumQualityLevels)physical|=m;}
  }
  config.sample_mask[f]=xe_transfer_identity_samples(physical,native2);
 }
 query_seconds=init_now()-started;
 if(!xe_transfer_identity_d3d12_native_ops(&init_forward) || !init_memory(&before)){status=E_FAIL;goto done;}
 ops=init_forward;ops.create_root=init_root;ops.create_pipeline=init_pipeline;
 ops.release_root=init_release_root;ops.release_pipeline=init_release_pipeline;ops.emit=init_emit;ops.free_shader=init_free;
 started=init_now();if(!xe_transfer_identity_d3d12_init(&ready,&config,&ops)){status=E_FAIL;goto done;}elapsed=init_now()-started;
 if(!init_memory(&after)){status=E_FAIL;goto done;}
 scale_classes=config.scale_x==1 && config.scale_y==1?1:4;
 for(f=0;f<4;++f){n=0;for(m=0;m<3;++m)if(config.sample_mask[f]&(1u<<m))++n;expected+=n*n*scale_classes;}
 if(!expected || init_profile.pipelines!=expected || init_profile.pipeline_live!=expected || init_profile.roots!=1 ||
  init_profile.root_live!=1 || init_profile.blob_live || init_profile.failed){status=E_FAIL;goto done;}
 for(f=0;f<4;++f)for(d=0;d<3;++d)for(s=0;s<3;++s)for(a=0;a<2;++a)for(b=0;b<2;++b) {
  found=xe_transfer_identity_d3d12_get(&ready,f,d,s,a,b);
  if((found!=NULL)!=((config.sample_mask[f]&(1u<<d)) && (config.sample_mask[f]&(1u<<s)))){status=E_FAIL;goto done;}
  ++lookups;
 }
 if(init_profile.pipelines!=expected || init_profile.blob_live){status=E_FAIL;goto done;}
 if(!identity_first_transfer(device,&ready,&config,&first)){status=E_FAIL;goto done;}
 if(init_profile.pipelines!=expected || init_profile.roots!=1 || init_profile.blob_live){status=E_FAIL;goto done;}
 if(!xe_transfer_identity_d3d12_close(&ready,&ops) || !init_memory(&closed)){status=E_FAIL;goto done;}
 if(init_profile.pipeline_live || init_profile.root_live || init_profile.blob_live || init_profile.failed){status=E_FAIL;goto done;}
 printf("INIT_D3D12 hardware%d vendor%u device%u scale%u,%u gamma%u native%u masks%u,%u,%u,%u\n",hardware,
  adapter_desc.VendorId,adapter_desc.DeviceId,config.scale_x,config.scale_y,gamma,native2,
  config.sample_mask[0],config.sample_mask[1],config.sample_mask[2],config.sample_mask[3]);
 printf("INIT_COST query_seconds=%.17g bank_seconds=%.17g emit_seconds=%.17g root_seconds=%.17g pso_seconds=%.17g emits=%u psos=%u emitted_bytes=%llu peak_blob_bytes=%llu pipeline_borrowed_bytes=%llu inline_bank_bytes=%llu lookups=%u\n",
  query_seconds,elapsed,init_profile.emit_seconds,init_profile.root_seconds,init_profile.pipeline_seconds,
  init_profile.emits,init_profile.pipelines,(unsigned long long)init_profile.emitted_bytes,(unsigned long long)init_profile.peak_blob,
  (unsigned long long)init_profile.pipeline_input_bytes,(unsigned long long)sizeof(ready),lookups);
 printf("INIT_PROCESS before_private=%llu ready_private=%llu closed_private=%llu before_rss=%llu ready_rss=%llu closed_rss=%llu\n",
  (unsigned long long)before.PrivateUsage,(unsigned long long)after.PrivateUsage,(unsigned long long)closed.PrivateUsage,
  (unsigned long long)before.WorkingSetSize,(unsigned long long)after.WorkingSetSize,(unsigned long long)closed.WorkingSetSize);
 printf("INIT_FIRST_DRAW record_seconds=%.17g submit_complete_seconds=%.17g validation_seconds=%.17g gpu_seconds=%.17g primitives=%llu vertices=%llu fragments=%llu pixels=%u\n",first.record_seconds,first.submit_complete_seconds,first.validation_seconds,first.gpu_seconds,(unsigned long long)first.primitives,(unsigned long long)first.vertices,(unsigned long long)first.fragments,first.pixels);
 printf("PASS: complete supported bank, correct first transfer, pure lookups, all acquired API objects released; driver-allocation bytes unobserved\n");status=S_OK;
done:
 if(ready.device && !xe_transfer_identity_d3d12_close(&ready,&ops))status=E_FAIL;
 if(device){if(FAILED(identity_debug_errors(device,debug)))status=E_FAIL;ID3D12Device_Release(device);}
 if(adapter)IDXGIAdapter_Release(adapter);if(factory)IDXGIFactory4_Release(factory);free(vertex);
 return FAILED(status)?1:0;
}
