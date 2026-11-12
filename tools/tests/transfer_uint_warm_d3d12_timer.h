/* Test-only host timing around completed GPU submissions and startup work. */
#ifndef XE_TEST_D3D12_WARM_TIMER_H
#define XE_TEST_D3D12_WARM_TIMER_H
static double warm_now(void) {
 LARGE_INTEGER now,frequency;
 if(!QueryPerformanceFrequency(&frequency) || !QueryPerformanceCounter(&now) || frequency.QuadPart<=0)exit(1);
 return (double)now.QuadPart/(double)frequency.QuadPart;
}
static double production_emit_seconds,production_pso_seconds,production_init_seconds;
static size_t production_module_bytes;
static HRESULT production_timed_pipeline(ID3D12Device *device,const D3D12_GRAPHICS_PIPELINE_STATE_DESC *desc,ID3D12PipelineState **out) {
 double started=warm_now();HRESULT status=xe_transfer_uint_d3d12_native_ops()->create_pipeline(device,desc,out);
 production_pso_seconds+=warm_now()-started;return status;
}
#endif
