/* Explicit physical-adapter test selection; no WARP fallback in hardware mode. */
#ifndef XE_TEST_TRANSFER_ADAPTER_H
#define XE_TEST_TRANSFER_ADAPTER_H
static HRESULT transfer_test_adapter(IDXGIFactory4 *factory,int hardware,IDXGIAdapter **out) {
 IDXGIAdapter1 *candidate=NULL;DXGI_ADAPTER_DESC1 desc;UINT index;HRESULT status;
 *out=NULL;
 if(!hardware)return IDXGIFactory4_EnumWarpAdapter(factory,&IID_IDXGIAdapter,(void**)out);
 for(index=0;;++index) {
  status=IDXGIFactory4_EnumAdapters1(factory,index,&candidate);
  if(FAILED(status))return status;
  status=IDXGIAdapter1_GetDesc1(candidate,&desc);
  if(FAILED(status)){IDXGIAdapter1_Release(candidate);return status;}
  if(!(desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)){*out=(IDXGIAdapter*)candidate;return S_OK;}
  IDXGIAdapter1_Release(candidate);candidate=NULL;
 }
}
#endif
