/* Test-only Windows producer-module lifetime, synchronous and single-owner. */
#ifndef XE_TEST_TRANSFER_D3D12_REVISION_LOAD_H
#define XE_TEST_TRANSFER_D3D12_REVISION_LOAD_H
#include "transfer_uint_d3d12_revision.h"
#include <windows.h>
#include <string.h>
typedef struct xe_test_d3d12_library {
 HMODULE module;const xe_test_d3d12_api *api;void *owner[3];
 ID3D12RootSignature *root[3];
} xe_test_d3d12_library;
static int xe_test_d3d12_load(xe_test_d3d12_library *library,const char *path) {
 xe_test_d3d12_lookup lookup=NULL;FARPROC symbol;const xe_test_d3d12_api *api;
 if(!library || library->module || !path || !*path || sizeof(lookup)!=sizeof(symbol))return 0;
 library->module=LoadLibraryA(path);if(!library->module)return 0;
 symbol=GetProcAddress(library->module,"xe_test_d3d12_revision_get");memcpy(&lookup,&symbol,sizeof(lookup));
 if(!lookup)return 0;
 api=lookup(XE_TEST_D3D12_ABI);
 if(!xe_test_d3d12_api_valid(api))return 0;
 library->api=api;return 1;
}
/* Caller has completed all GPU uses. Close owners BEFORE unloading their CRT. */
static int xe_test_d3d12_unload(xe_test_d3d12_library *library) {
 unsigned i;if(!library)return 0;
 if(library->api)for(i=0;i<3;++i)if(library->owner[i]) {
  library->api->close(library->owner[i]);library->owner[i]=NULL;library->root[i]=NULL;
 }
 if(library->module && !FreeLibrary(library->module))return 0;
 memset(library,0,sizeof(*library));return 1;
}
#endif
