/* Validate every emitted native module before PSO creation, test-only ABI. */
#ifndef XE_TEST_TRANSFER_UINT_PRODUCTION_D3D12_BANK_H
#define XE_TEST_TRANSFER_UINT_PRODUCTION_D3D12_BANK_H
extern void *xe_test_validator_open(const char*);
extern int xe_test_validator_check(void*,void*,size_t,void (*)(const char*,size_t));
extern void xe_test_validator_close(void*);
static void *production_validator;
static unsigned production_modules;
static void production_validation_report(const char *data,size_t bytes){fwrite(data,1,bytes,stderr);}
#ifdef XE_TEST_D3D12_WARM
#include "transfer_uint_warm_d3d12_timer.h"
#endif
static int production_checked_emit(const xe_transfer_uint_options *options,const xe_transfer_address_key *key,
 unsigned format,XeDxilShaderBinary *out) {
#ifdef XE_TEST_D3D12_WARM
 double started=warm_now();
#endif
#ifdef XE_TEST_D3D12_WARM
 if(getenv("TRANSFER_WARM_DYNAMIC_CONTROL")) {
  if(!xe_dxil_transfer_uint_shader(options,out))return 0;
 } else
#endif
 if(!xe_dxil_transfer_uint_shader_shape(options,key,format,out))return 0;
#ifdef XE_TEST_D3D12_WARM
 production_emit_seconds+=warm_now()-started;production_module_bytes+=out->size;
#endif
 if(!xe_test_validator_check(production_validator,out->data,out->size,production_validation_report))return 0;
 ++production_modules;return 1;
}
static HRESULT make_shaped_bank(ID3D12Device *device,bank *b,const char *prefix,
 int native2,const D3D12_GRAPHICS_PIPELINE_STATE_DESC *base) {
 xe_transfer_uint_d3d12_config config={0};xe_transfer_uint_d3d12_ops ops=*xe_transfer_uint_d3d12_native_ops();
 const char *validator=getenv("XE_TEST_DXIL_VALIDATOR");unsigned n,scale;HRESULT status=E_FAIL;
#ifdef XE_TEST_D3D12_WARM
 double started;
#endif
 (void)prefix;
 if(!validator || !*validator)return E_FAIL;
 production_validator=xe_test_validator_open(validator);if(!production_validator)return E_FAIL;
 ops.emit=production_checked_emit;
#ifdef XE_TEST_D3D12_WARM
 ops.create_pipeline=production_timed_pipeline;
#endif
 config.device=device;config.vertex=base->VS.pShaderBytecode;config.vertex_bytes=base->VS.BytecodeLength;
#ifdef XE_TEST_D3D12_WARM
 if(!native2)goto done;
 for(n=1;n<2;++n)for(scale=0;scale<3;++scale) {
#else
 for(n=0;n<(native2?2u:1u);++n)for(scale=0;scale<4;++scale) {
#endif
  config.native2=n;config.scale_x=scale==1?2:scale==2?3:1;
  config.scale_y=scale==1?2:scale==2?5:scale==3?3:1;
#ifdef XE_TEST_D3D12_WARM
  config.scale_x=config.scale_y=1u<<scale;started=warm_now();
#endif
  if(!xe_transfer_uint_d3d12_init(&b->production[n][scale],&config,&ops))goto done;
#ifdef XE_TEST_D3D12_WARM
  production_init_seconds+=warm_now()-started;
#endif
 }
#ifdef XE_TEST_D3D12_WARM
 if(production_modules!=324u)goto done;
 fprintf(stderr,"D3D12_CANDIDATE_INIT banks3 modules%u bytes%llu emission_seconds%.9g pso_seconds%.9g total_seconds%.9g\n",production_modules,(unsigned long long)production_module_bytes,production_emit_seconds,production_pso_seconds,production_init_seconds);
#else
 if(production_modules!=(native2?936u:468u))goto done;
#endif
 fprintf(stderr,"PASS: SDK validation of %u actual bank modules before PSO creation\n",production_modules);
 status=S_OK;
done:
 xe_test_validator_close(production_validator);production_validator=NULL;return status;
}
static void close_shaped_bank(bank *b) {
 unsigned n,scale;
 for(n=0;n<2;++n)for(scale=0;scale<4;++scale)
  if(!xe_transfer_uint_d3d12_close(&b->production[n][scale],xe_transfer_uint_d3d12_native_ops()))exit(1);
}
#endif
