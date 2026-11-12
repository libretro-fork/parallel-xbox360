/* Real DLL header/entry validation; no GPU or framebuffer is required. C89. */
#define COBJMACROS
#include "transfer_uint_d3d12_revision_load.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"ABI line%u: %s\n",(unsigned)__LINE__,#x);exit(1);}}while(0)
int main(int argc,char **argv) {
 xe_test_d3d12_library library={0};xe_test_d3d12_api changed;
 xe_test_d3d12_lookup lookup=NULL;FARPROC symbol;xe_test_d3d12_key key={0};xe_test_d3d12_config config={0};
 unsigned char packet[XE_TEST_D3D12_MAX_PACKET];
 CHECK(argc==2);CHECK(!xe_test_d3d12_load(&library,""));
 CHECK(xe_test_d3d12_load(&library,argv[1]));CHECK(library.api);
 CHECK(!xe_test_d3d12_load(&library,argv[1]));
 symbol=GetProcAddress(library.module,"xe_test_d3d12_revision_get");memcpy(&lookup,&symbol,sizeof(lookup));CHECK(lookup);
 CHECK(!lookup(0) && !lookup(XE_TEST_D3D12_ABI+1));
 CHECK(!xe_test_d3d12_api_valid(NULL));
 changed=*library.api;changed.version=0;CHECK(!xe_test_d3d12_api_valid(&changed));
 changed=*library.api;changed.bytes=0;CHECK(!xe_test_d3d12_api_valid(&changed));
 changed=*library.api;changed.config_bytes++;CHECK(!xe_test_d3d12_api_valid(&changed));
 changed=*library.api;changed.key_bytes++;CHECK(!xe_test_d3d12_api_valid(&changed));
 changed=*library.api;changed.packet_bytes++;CHECK(!xe_test_d3d12_api_valid(&changed));
 changed=*library.api;changed.kind=0;CHECK(!xe_test_d3d12_api_valid(&changed));
 changed=*library.api;changed.constants_root=2;CHECK(!xe_test_d3d12_api_valid(&changed));
 changed=*library.api;changed.image_root=2;CHECK(!xe_test_d3d12_api_valid(&changed));
 changed=*library.api;changed.open=NULL;CHECK(!xe_test_d3d12_api_valid(&changed));
 changed=*library.api;changed.root=NULL;CHECK(!xe_test_d3d12_api_valid(&changed));
 changed=*library.api;changed.pipeline=NULL;CHECK(!xe_test_d3d12_api_valid(&changed));
 changed=*library.api;changed.packet=NULL;CHECK(!xe_test_d3d12_api_valid(&changed));
 changed=*library.api;changed.close=NULL;CHECK(!xe_test_d3d12_api_valid(&changed));
 CHECK(!library.api->open(NULL) && !library.api->open(&config));
 CHECK(!library.api->root(NULL) && !library.api->pipeline(NULL,&key));
 memset(packet,0xa5,sizeof(packet));CHECK(!library.api->packet(NULL,&key,packet,sizeof(packet)));
 CHECK(packet[0]==0xa5);library.api->close(NULL);
 CHECK(xe_test_d3d12_unload(&library));CHECK(xe_test_d3d12_unload(&library));
 CHECK(!library.api && !library.module);
 printf("PASS: D3D12 revision ABI headers, wrong versions/sizes, callback completeness, null ownership, unload\n");return 0;
}
