/* C89 borrowed-wire/owned-snapshot tests; injected allocation failure. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned allocations, releases;
static int fail_allocation;
static void *test_alloc(void *p, size_t n) {
  ++allocations; return fail_allocation ? NULL : realloc(p, n);
}
static void test_free(void *p) { if (p) ++releases; free(p); }
#define XE_PE_REALLOC test_alloc
#define XE_PE_FREE test_free
#include "core_pe_image.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"PE line %d: %s\n",__LINE__,#x); return 1; } } while (0)
static uint8_t bytes[65537], saved[65536];
static void put16(uint8_t *p, unsigned n) { p[0]=(uint8_t)n; p[1]=(uint8_t)(n>>8); }
static void put32(uint8_t *p, uint32_t n) {
  p[0]=(uint8_t)n; p[1]=(uint8_t)(n>>8); p[2]=(uint8_t)(n>>16); p[3]=(uint8_t)(n>>24);
}
static void fixture(uint8_t *p) {
  memset(p,0,65536); memcpy(p,"MZ\220\000",4); put32(p+60,64);
  memcpy(p+64,"PE\000\000",4); put16(p+68,0x1f2); put16(p+70,2);
  put16(p+84,224); put16(p+86,0x100); put16(p+88,0x10b); put16(p+156,14);
  memcpy(p+312,".text",5); put32(p+320,256); put32(p+324,4096);
  put32(p+328,512); put32(p+332,0x800); put32(p+348,0x60000020U);
  memcpy(p+352,"eightchr",8); put32(p+360,512); put32(p+364,8192);
  put32(p+368,256); put32(p+372,0x1000); put32(p+388,0xc0000040U);
}
#ifdef XE_PE_BRIDGE
extern int xe_test_pe_module(const uint8_t *, size_t, const xe_pe_section *);
#endif
int main(void) {
  xe_pe_sections owner = {NULL,0,0};
  xe_pe_view view, untouched;
  xe_pe_section expected[2], *old;
  size_t i, length;
  uint32_t rng=12345;
  unsigned fault, before;
  uint8_t *p=bytes+1;
  fixture(p); memcpy(saved,p,65536);
  CHECK(xe_pe_probe(p,65536) && !xe_pe_probe(p,3) && !xe_pe_probe(NULL,4));
  CHECK(xe_pe_validate(p,65536,0x82000000U,&view));
  CHECK(view.sections==p+312 && view.count==2 && view.base==0x82000000U);
  CHECK(xe_pe_sections_read(&owner,&view) && allocations==1 && owner.size==2);
  CHECK(!strcmp(owner.data[0].name,".text") && owner.data[0].address==0x82001000U &&
      owner.data[0].size==256 && owner.data[0].raw_size==512 &&
      owner.data[0].raw_address==0x800 && owner.data[0].flags==0x60000020U);
  CHECK(!strcmp(owner.data[1].name,"eightchr") && owner.data[1].address==0x82002000U);
  memcpy(expected,owner.data,sizeof(expected)); old=owner.data;
  memset(p+312,0,8); CHECK(!strcmp(owner.data[0].name,".text")); memcpy(p,saved,65536);
  for(i=0;i<1000;++i) CHECK(xe_pe_sections_read(&owner,&view));
  CHECK(allocations==1 && owner.data==old);
  put16(p+70,3); CHECK(xe_pe_validate(p,65536,0x82000000U,&view)); fail_allocation=1;
  CHECK(!xe_pe_sections_read(&owner,&view) && owner.data==old && owner.size==2 &&
      !memcmp(owner.data,expected,sizeof(expected))); fail_allocation=0; memcpy(p,saved,65536);
  /* Complete preflight does not publish a partial view on any late failure. */
  memset(&untouched,0xa7,sizeof(untouched));
  for(fault=0;fault<15;++fault) {
    memcpy(p,saved,65536); memcpy(&view,&untouched,sizeof(view));
    switch(fault) {
      case 0:p[0]=0;break;
      case 1:put32(p+60,0xffffffffU);break;
      case 2:put32(p+60,65535);break;
      case 3:p[64]=0;break;
      case 4:put16(p+68,0);break;
      case 5:put16(p+86,0);break;
      case 6:put16(p+84,223);break;
      case 7:put16(p+88,0);break;
      case 8:put16(p+156,0);break;
      case 9:put16(p+70,65535);break;
      case 10:put32(p+364,65536);break;
      case 11:put32(p+360,0xffffffffU);break;
      case 12:put32(p+368,0xffffffffU);break;
      case 13:put32(p+364,0xffffffffU);break;
      default:put32(p+60,65536-247);break;
    }
    CHECK(!xe_pe_validate(p,65536,0x82000000U,&view));
    CHECK(!memcmp(&view,&untouched,sizeof(view)));
    CHECK(owner.data==old && !memcmp(owner.data,expected,sizeof(expected)));
  }
  memcpy(p,saved,65536);
  for(length=0;length<392;++length) CHECK(!xe_pe_validate(p,length,0x82000000U,&view));
  CHECK(!xe_pe_validate(NULL,65536,0,&view));
  CHECK(!xe_pe_validate(p,65536,0xffff8000U,&view));
  CHECK(xe_pe_validate(p,65536,0x82000000U,&view));
#ifdef XE_PE_BRIDGE
  CHECK(xe_test_pe_module(p,65536,expected));
#endif
  before=allocations;
  for(i=0;i<10000;++i) {
    memcpy(p,saved,65536);
    rng^=rng<<13; rng^=rng>>17; rng^=rng<<5;
    if (i & 1) put32(p+60,rng);
    else put32(p+360+(i%3)*4,rng);
    if(xe_pe_validate(p,65536,0x82000000U,&view)) CHECK(xe_pe_sections_read(&owner,&view));
  }
  CHECK(allocations==before);
  memcpy(p,saved,65536); put16(p+70,0);
  CHECK(xe_pe_validate(p,65536,0x82000000U,&view));
  CHECK(xe_pe_sections_read(&owner,&view) && !owner.size && owner.data==old);
  xe_pe_sections_free(&owner); xe_pe_sections_free(&owner);
  CHECK(!owner.data && !owner.size && !owner.capacity && releases==1);
  puts("PE: unaligned borrowed tables, bounded spans, retained snapshots and allocation failure pass");
  return 0;
}
