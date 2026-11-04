#include "core_file_blocks.h"
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %u\n",(unsigned)__LINE__); exit(1); } } while(0)
static unsigned allocations, live, fail_alloc;
void *xe_block_test_alloc(size_t size) {
   size_t *p;
   ++allocations;
   if (fail_alloc) { fail_alloc=0; return NULL; }
   p=(size_t *)malloc(sizeof(*p)+size); if(!p) return NULL;
   *p=size; ++live; return p+1;
}
void xe_block_test_free(void *p) { if(p) { --live; free((size_t *)p-1); } }
static unsigned char files[3][2048];
struct state { unsigned reads, validations, fail_read, oversize, short_read; unsigned char *expected; };
static int validate(void *opaque,size_t file,size_t offset,size_t length) {
   struct state *s=(struct state *)opaque; ++s->validations;
   return file<3 && offset<=sizeof(files[0]) && length<=sizeof(files[0])-offset;
}
static size_t read_block(void *opaque,size_t file,size_t offset,void *out,size_t length) {
   struct state *s=(struct state *)opaque;
   size_t n=length;
   ++s->reads; CHECK(out==s->expected);
   if(s->fail_read==s->reads) return 0;
   if(s->oversize) return length+1;
   if(s->short_read && n>s->short_read) n=s->short_read;
   memcpy(out,files[file]+offset,n); s->expected+=n; return n;
}
static unsigned char oracle(const xe_file_block *rows,size_t count,size_t index) {
   size_t i;
   for(i=0;i<count;++i) {
      if(index<rows[i].length) return files[rows[i].file][rows[i].offset+index];
      index-=rows[i].length;
   }
   CHECK(0); return 0;
}
int main(void) {
   xe_file_block rows[64], fragmented[]={{0,4,4},{0,8,4},{1,0,8},{0,6,4}};
   xe_file_blocks list={NULL,0,0};
   struct state state;
   unsigned char out[1026];
   size_t i,j,offset,length,n,before;
   const xe_file_block *old;
   for(i=0;i<3;++i) for(j=0;j<sizeof(files[0]);++j) files[i][j]=(unsigned char)(i*71+j*37);
   for(i=0;i<64;++i) { rows[i].file=0; rows[i].offset=i*16; rows[i].length=16; }
   memset(&state,0,sizeof(state)); memset(out,0xAD,sizeof(out)); state.expected=out+1;
   before=allocations;
   CHECK(xe_file_blocks_read(rows,64,1024,0,out+1,1024,validate,read_block,&state,&n));
   CHECK(n==1024 && state.reads==1 && state.validations==1 && allocations==before);
   CHECK(out[0]==0xAD && out[1025]==0xAD && !memcmp(out+1,files[0],1024));
   for(offset=0;offset<23;++offset) for(length=0;length<25;++length) {
      memset(&state,0,sizeof(state)); memset(out,0xAD,sizeof(out)); state.expected=out+1;
      CHECK(xe_file_blocks_read(fragmented,4,20,offset,out+1,length,validate,read_block,&state,&n)==(offset<20));
      j=offset<20 ? 20-offset : 0; if(j>length) j=length;
      CHECK(n==j && allocations==before && out[0]==0xAD);
      for(i=0;i<sizeof(out)-1;++i) CHECK(out[i+1]==(i<j?oracle(fragmented,4,offset+i):0xAD));
   }
   fragmented[3].file=9;
   memset(&state,0,sizeof(state)); memset(out,0xAD,sizeof(out)); state.expected=out+1;
   CHECK(!xe_file_blocks_read(fragmented,4,20,0,out+1,20,validate,read_block,&state,&n) && !n && !state.reads);
   for(i=0;i<sizeof(out);++i) CHECK(out[i]==0xAD);
   fragmented[3].file=0; fragmented[3].offset=2047;
   CHECK(!xe_file_blocks_read(fragmented,4,20,0,out+1,20,validate,read_block,&state,&n) && !n && !state.reads);
   fragmented[3].offset=6;
   for(i=1;i<=3;++i) {
      memset(&state,0,sizeof(state)); memset(out,0xAD,sizeof(out)); state.expected=out+1;
      if(i==1) state.short_read=3;
      if(i==2) state.fail_read=2;
      if(i==3) state.oversize=1;
      CHECK(!xe_file_blocks_read(fragmented,4,20,0,out+1,20,validate,read_block,&state,&n));
      CHECK(n==(i==1?3:i==2?8:0));
      for(j=0;j<n;++j) CHECK(out[j+1]==oracle(fragmented,4,j));
      CHECK(out[n+1]==0xAD && out[0]==0xAD);
   }
   memset(&state,0,sizeof(state)); state.expected=out+1;
   CHECK(!xe_file_blocks_read(rows,1,17,0,out+1,17,validate,read_block,&state,&n) && !n && !state.reads);
   rows[1].offset=SIZE_MAX-4; rows[1].length=8;
   CHECK(!xe_file_blocks_read(rows,2,24,0,out+1,24,validate,read_block,&state,&n) && !n && !state.reads);
   CHECK(!xe_file_blocks_read(rows,SIZE_MAX,1,0,out,1,validate,read_block,&state,&n));
   CHECK(!xe_file_blocks_read(NULL,0,1,0,out,1,validate,read_block,&state,&n));
   CHECK(xe_file_blocks_read(NULL,0,1,0,NULL,0,NULL,NULL,NULL,&n) && !n);
   for(i=0;i<64;++i) CHECK(xe_file_blocks_append(&list,0,i*16,16)==1);
   CHECK(list.count==1 && list.capacity==1 && list.data[0].length==1024 && live==1);
   old=list.data; fail_alloc=1;
   CHECK(xe_file_blocks_append(&list,1,1024,16)==-1 && list.data==old && list.count==1);
   CHECK(xe_file_blocks_append(&list,1,1024,16)==1 && list.count==2);
   CHECK(xe_file_blocks_append(&list,1,1028,8)==1 && list.count==3);
   CHECK(xe_file_blocks_append(&list,1,1036,4)==1 && list.count==3 && list.data[2].length==12);
   CHECK(xe_file_blocks_append(&list,0,1040,2)==1 && list.count==4);
   CHECK(xe_file_blocks_append(&list,1,SIZE_MAX,1)==0 && list.count==4);
   CHECK(xe_file_blocks_append(&list,9,SIZE_MAX,0)==1 && list.count==4);
   xe_file_blocks_free(&list); xe_file_blocks_free(&list);
   CHECK(!live && !list.count && !list.capacity && !list.data);
   puts("PASS: direct coalesced reads, byte oracle, full preflight, short/error bounds and C89 extent ownership");
   return 0;
}
