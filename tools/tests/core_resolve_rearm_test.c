#include <stdio.h>
#include <stdlib.h>
#include "core_resolve_rearm.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"rearm C89 line %d: %s\n",__LINE__,#x); exit(1); } } while (0)
static xe_resolve_rearm state;
static unsigned char expected[2][XE_MEMEXPORT_PAGE_COUNT];
static uint32_t pending;
static uint32_t random_word=0x17ab9351u;
static uint32_t next_word(void) { random_word ^= random_word << 13; random_word ^= random_word >> 17; random_word ^= random_word << 5; return random_word; }
static int reference_mark(unsigned char *pages,uint32_t base,uint32_t size) {
  uint64_t end=(uint64_t)base+size;
  uint32_t i;
  if (!size || base>=XE_MEMEXPORT_BUFFER_SIZE) return 0;
  if (end>XE_MEMEXPORT_BUFFER_SIZE) end=XE_MEMEXPORT_BUFFER_SIZE;
  for(i=base/4096;i<=(uint32_t)((end-1)/4096);++i) pages[i]=1;
  return 1;
}
static void mark(uint32_t base,uint32_t size) {
  CHECK(xe_resolve_rearm_mark(&state,base,size)==reference_mark(expected[pending],base,size));
}
static unsigned drain(int interleave) {
  xe_memexport_cursor cursor, rejected;
  xe_memexport_run run;
  uint32_t old=pending,i,last=0;
  unsigned count=0,any=0;
  for(i=0;i<XE_MEMEXPORT_PAGE_COUNT;++i) any|=expected[old][i];
  if(!any) { CHECK(!xe_resolve_rearm_begin(&state,&cursor)); return 0; }
  CHECK(xe_resolve_rearm_begin(&state,&cursor));pending^=1;
  CHECK(cursor.words==state.borrowed->words);
  memset(&rejected,0xa3,sizeof(rejected));
  CHECK(!xe_resolve_rearm_begin(&state,&rejected));
  while(xe_memexport_next(&cursor,&run)) {
    CHECK(run.size && !(run.base&4095) && !(run.size&4095));
    CHECK(run.base>=last && run.size<=XE_MEMEXPORT_BUFFER_SIZE-run.base);
    for(i=last/4096;i<run.base/4096;++i) CHECK(!expected[old][i]);
    for(i=run.base/4096;i<(run.base+run.size)/4096;++i) { CHECK(expected[old][i]); expected[old][i]=0; }
    last=run.base+run.size;++count;
    if(interleave) mark(next_word()&(XE_MEMEXPORT_BUFFER_SIZE-1),next_word()%16384);
  }
  for(i=0;i<XE_MEMEXPORT_PAGE_COUNT;++i) CHECK(!expected[old][i]);
  xe_resolve_rearm_finish(&state); CHECK(!state.borrowed);
  for(i=0;i<XE_MEMEXPORT_PAGE_COUNT/64;++i) CHECK(!state.maps[old].words[i]);
  xe_resolve_rearm_finish(&state); /* Idempotent when no batch is borrowed. */
  return count;
}
/* Simulate PhysicalHeap::EnableAccessCallbacks bounds/page snapping. Compare
 * raw legacy ranges with the coalesced guest-page runs for real alias layouts. */
struct range {uint32_t base,size;};
static struct range raw[4096];
static unsigned char host_old[XE_MEMEXPORT_PAGE_COUNT+16],host_new[XE_MEMEXPORT_PAGE_COUNT+16];
static void alias_mark(unsigned char *host,uint32_t base,uint32_t size,uint32_t physical_offset,uint32_t heap_size,uint32_t host_offset,uint32_t shift) {
  uint64_t end=(uint64_t)base+size,heap_end=(uint64_t)physical_offset+heap_size;
  uint32_t first,last,i;
  if(base<physical_offset) base=physical_offset;
  if(end>heap_end) end=heap_end;
  if(!size || end<=base) return;
  first=(base-physical_offset+host_offset)>>shift;
  last=(uint32_t)((end-physical_offset+host_offset-1)>>shift);
  for(i=first;i<=last;++i) { CHECK(i<sizeof(host_old)); host[i]=1; }
}
static void aliases(void) {
  uint32_t i,shift,alias;
  static const uint32_t offsets[4]={0,0,0x1000,0};
  static const uint32_t sizes[4]={0x20000000,0x20000000,0x1fd00000,0xc80000};
  xe_resolve_rearm_reset(&state);memset(expected,0,sizeof(expected));pending=0;
  for(i=0;i<4096;++i) {
    raw[i].base=next_word()&(XE_MEMEXPORT_BUFFER_SIZE-1);
    raw[i].size=next_word()%65536+1;mark(raw[i].base,raw[i].size);
  }
  raw[0].base=0;raw[0].size=1;mark(0,1);
  raw[1].base=XE_MEMEXPORT_BUFFER_SIZE-1;raw[1].size=1;mark(raw[1].base,1);
  /* Rebuild after overwriting two raw records so union equality is exact. */
  xe_resolve_rearm_reset(&state);memset(expected,0,sizeof(expected));
  for(i=0;i<4096;++i)mark(raw[i].base,raw[i].size);
  for(shift=12;shift<=16;shift+=2) for(alias=0;alias<4;++alias) {
    xe_memexport_cursor cursor;xe_memexport_run run;
    uint32_t host_offset=alias==2?4096:0;
    memset(host_old,0,sizeof(host_old));memset(host_new,0,sizeof(host_new));
    for(i=0;i<4096;++i)alias_mark(host_old,raw[i].base,raw[i].size,offsets[alias],sizes[alias],host_offset,shift);
    CHECK(xe_memexport_window(state.pending,0,XE_MEMEXPORT_BUFFER_SIZE,&cursor));
    while(xe_memexport_next(&cursor,&run))alias_mark(host_new,run.base,run.size,offsets[alias],sizes[alias],host_offset,shift);
    CHECK(!memcmp(host_old,host_new,sizeof(host_old)));
  }
  drain(0);
}
int main(void) {
  uint32_t i,j;
  memset(&state,0xa7,sizeof(state));xe_resolve_rearm_reset(&state);
  CHECK(!drain(0));mark(0,0);mark(UINT32_MAX,UINT32_MAX);CHECK(!drain(0));
  mark(63*4096+1,8192);mark(63*4096+1,8192);CHECK(drain(0)==1);
  mark(XE_MEMEXPORT_BUFFER_SIZE-1,UINT32_MAX);CHECK(drain(1)==1);drain(0);
  for(i=0;i<8193;++i) mark(i*8192,1);
  CHECK(drain(0)==8193);
  mark(0,XE_MEMEXPORT_BUFFER_SIZE);CHECK(drain(1)==1);drain(0);
  for(i=0;i<1000;++i) {
    for(j=0;j<200;++j)mark(next_word()&(XE_MEMEXPORT_BUFFER_SIZE-1),next_word()%16384);
    drain(1);
  }
  drain(0);aliases();
  mark(4095,2); {xe_memexport_cursor cursor;CHECK(xe_resolve_rearm_begin(&state,&cursor));}
  xe_resolve_rearm_reset(&state);memset(expected,0,sizeof(expected));pending=0;
  CHECK(!drain(0));mark(4096,1);CHECK(drain(0)==1);
  printf("Rearm C89: exact union, 8193 disjoint pending ranges, duplicate/unaligned/aperture edges, 1000 interleaved drains and 12 host-alias/page configurations pass; storage=%lu bytes\n",(unsigned long)sizeof(state));return 0;
}
