/* Allocation-free borrowed restore plans. MSVC C89. */
#include "libretro/core_heap_state.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)) {fprintf(stderr,"heap plan line %u\n",(unsigned)__LINE__);exit(1);}} while(0)
static void decode(const void *record,xe_heap_page *page) {memcpy(page,record,sizeof(*page));}
static void check_pattern(unsigned mask) {
  unsigned char storage[8*sizeof(xe_heap_page)+1];
  xe_heap_state view;
  xe_heap_page page;
  xe_heap_scan scan={0,0};
  xe_heap_run run;
  unsigned i,seen=0,expected=0,runs=0;
  for(i=0;i<8;++i) {
    memset(&page,0,sizeof(page));
    if(mask&(1u<<i)) {page.base=i;page.count=1;page.state=3;++expected;}
    memcpy(storage+1+i*sizeof(page),&page,sizeof(page));
  }
  CHECK(xe_heap_state_plan(&view,storage+1,8,sizeof(page),4096,1,decode));
  CHECK(view.entries==storage+1 && !view.data && view.data_bytes==(size_t)expected*4096);
  CHECK(view.unreserved==8-expected);
  while(xe_heap_state_next(&view,&scan,&run)) {
    CHECK(run.count && run.offset==(size_t)seen*4096 && run.bytes==(size_t)run.count*4096);
    for(i=run.first;i<run.first+run.count;++i) {CHECK(i<8 && (mask&(1u<<i)));++seen;}
    ++runs;
  }
  CHECK(seen==expected && runs<=4 && scan.offset==view.data_bytes);
  CHECK(!xe_heap_state_next(&view,&scan,&run));
  CHECK(xe_heap_state_plan(&view,storage+1,8,sizeof(page),4096,0,decode));
  scan.page=scan.offset=0;CHECK(!view.data_bytes && !xe_heap_state_next(&view,&scan,&run));
}
int main(void) {
  xe_heap_page pages[16];
  xe_heap_state view,original;
  xe_heap_scan scan={0,0};
  xe_heap_run run;
  unsigned mask,i;
  for(mask=0;mask<256;++mask) check_pattern(mask);
  memset(pages,0,sizeof(pages));
  for(i=0;i<16;++i) {pages[i].base=0;pages[i].count=16;pages[i].state=3;}
  CHECK(xe_heap_state_plan(&view,pages,16,sizeof(pages[0]),4096,1,decode));
  CHECK(xe_heap_state_next(&view,&scan,&run) && run.first==0 && run.count==16 && run.bytes==65536);
  CHECK(!xe_heap_state_next(&view,&scan,&run));
  original=view;pages[15].reserved=1;
  CHECK(!xe_heap_state_plan(&view,pages,16,sizeof(pages[0]),4096,1,decode));
  CHECK(view.entries==original.entries && view.data==original.data &&
      view.pages==original.pages && view.stride==original.stride &&
      view.page_bytes==original.page_bytes && view.data_bytes==original.data_bytes &&
      view.unreserved==original.unreserved && view.contents==original.contents && view.decode==original.decode);pages[15].reserved=0;
  pages[15].base=16;CHECK(!xe_heap_state_plan(&view,pages,16,sizeof(pages[0]),4096,1,decode));
  pages[15].base=0;pages[15].count=17;CHECK(!xe_heap_state_plan(&view,pages,16,sizeof(pages[0]),4096,1,decode));
  pages[15].count=0;CHECK(!xe_heap_state_plan(&view,pages,16,sizeof(pages[0]),4096,1,decode));
  pages[15].count=16;pages[15].state=4;CHECK(!xe_heap_state_plan(&view,pages,16,sizeof(pages[0]),4096,1,decode));
  pages[15].state=3;
  CHECK(!xe_heap_state_plan(&view,NULL,16,sizeof(pages[0]),4096,1,decode));
  CHECK(!xe_heap_state_plan(&view,pages,SIZE_MAX,sizeof(pages[0]),4096,1,decode));
  CHECK(!xe_heap_state_plan(&view,pages,16,SIZE_MAX,4096,1,decode));
  CHECK(!xe_heap_state_plan(&view,pages,16,sizeof(pages[0]),0,1,decode));
  CHECK(xe_heap_state_plan(&view,pages,16,sizeof(pages[0]),UINT32_MAX,1,decode));
  scan.page=scan.offset=0;
  for(i=0;i<16;++i) CHECK(xe_heap_state_next(&view,&scan,&run) && run.count==1 && run.bytes==UINT32_MAX);
  CHECK(!xe_heap_state_next(&view,&scan,&run));
  for(i=0;i<16;++i) pages[i].state=1;
  pages[8].state=2;
  CHECK(xe_heap_state_plan(&view,pages,16,sizeof(pages[0]),4096,1,decode));
  CHECK(!view.unreserved && view.data_bytes==4096);
  scan.page=scan.offset=0;
  CHECK(xe_heap_state_next(&view,&scan,&run) && run.first==8 && run.count==1);
  CHECK(!xe_heap_state_next(&view,&scan,&run));
  CHECK(xe_heap_state_plan(&view,NULL,0,sizeof(pages[0]),4096,1,decode));
  CHECK(!view.data_bytes && !view.unreserved);
  scan.page=scan.offset=0;CHECK(!xe_heap_state_next(&view,&scan,&run));
  puts("PASS: borrowed heap plans, all 256 sparse patterns, alias metadata, failed publication and native commit overflow splitting");return 0;
}
