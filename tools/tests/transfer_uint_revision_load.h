/* Test-only module owner. Parent resources stay alive through GPU completion. */
#ifndef XE_TEST_TRANSFER_REVISION_LOAD_H
#define XE_TEST_TRANSFER_REVISION_LOAD_H
#include "transfer_uint_revision.h"
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif
typedef struct xe_test_transfer_revision_library {
#ifdef _WIN32
 HMODULE module;
#else
 void *module;
#endif
 const xe_test_transfer_revision_api *api;
 void *owner[3];VkPipelineLayout layout[3];
} xe_test_transfer_revision_library;
static int xe_test_transfer_revision_load(xe_test_transfer_revision_library *library,const char *path) {
 xe_test_transfer_revision_entry entry=NULL;
#ifdef _WIN32
 FARPROC symbol;
 library->module=LoadLibraryA(path);if(!library->module)return 0;
 symbol=GetProcAddress(library->module,"xe_test_transfer_revision_get");
#else
 void *symbol;
 library->module=dlopen(path,RTLD_NOW|RTLD_LOCAL);if(!library->module)return 0;
 symbol=dlsym(library->module,"xe_test_transfer_revision_get");
#endif
 memcpy(&entry,&symbol,sizeof(entry));if(!entry)return 0;
 library->api=entry(XE_TEST_TRANSFER_REVISION_ABI,sizeof(*library->api));
 return library->api && library->api->version==XE_TEST_TRANSFER_REVISION_ABI &&
  library->api->packet_bytes==XE_TEST_TRANSFER_PACKET_BYTES && library->api->open &&
  library->api->layout && library->api->pipeline && library->api->packet && library->api->close;
}
static int xe_test_transfer_revision_unload(xe_test_transfer_revision_library *library) {
 unsigned i;
 if(library->api)for(i=0;i<3;++i)if(library->owner[i]) {
  if(!library->api->close(library->owner[i]))return 0;
  library->owner[i]=NULL;
 }
 if(library->module) {
#ifdef _WIN32
  if(!FreeLibrary(library->module))return 0;
#else
  if(dlclose(library->module))return 0;
#endif
 }
 memset(library,0,sizeof(*library));return 1;
}
#endif
