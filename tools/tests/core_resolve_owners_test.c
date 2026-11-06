/* Registry invariants, collision stress and independent sorted-age reference. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core_resolve_owners.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); return 1; } } while (0)
static uint32_t random_value = 0x678123;
static uint32_t random_u32(void) { random_value = random_value * UINT32_C(1664525) + UINT32_C(1013904223); return random_value; }
static int compare_u32(const void *a,const void *b) {
  uint32_t x=*(const uint32_t*)a,y=*(const uint32_t*)b;
  return (x>y)-(x<y);
}
static int validate(const xe_resolve_owners *s) {
  uint8_t seen[XE_RESOLVE_OWNER_CAPACITY];
  uint32_t i, live=0, free_count=0, indexed=0;
  uint16_t free_id=s->free_head;
  memset(seen,0,sizeof(seen));
  for(i=xe_resolve_owners_next(s,0);i<XE_RESOLVE_OWNER_CAPACITY;i=xe_resolve_owners_next(s,i+1)) {
    CHECK(!seen[i]);seen[i]=1;++live;
    CHECK(xe_resolve_owners_find_id(s,s->keys[i])==i);
    CHECK(s->location[i]<XE_RESOLVE_OWNER_BUCKETS && s->buckets[s->location[i]]==i);
  }
  for(i=0;i<XE_RESOLVE_OWNER_BUCKETS;++i) {
    uint16_t id=s->buckets[i];
    if(id==XE_RESOLVE_OWNER_NONE)continue;
    CHECK(id<XE_RESOLVE_OWNER_CAPACITY && seen[id]==1);
    seen[id]=2;++indexed;
  }
  while(free_id!=XE_RESOLVE_OWNER_NONE) {
    CHECK(free_id<XE_RESOLVE_OWNER_CAPACITY && !seen[free_id]);
    seen[free_id]=3;++free_count;free_id=s->next_free[free_id];
  }
  CHECK(live==s->count && indexed==live && free_count+live==XE_RESOLVE_OWNER_CAPACITY);
  for(i=0;i<XE_RESOLVE_OWNER_CAPACITY;++i)CHECK(seen[i]==2 || seen[i]==3);
  return 0;
}
static int selection(void) {
  uint32_t original[4096],work[4096],sorted[4096];
  uint32_t n,mode,rank,i;
  size_t cases=0;
  CHECK(xe_resolve_age_select(work,0,0)==0 && xe_resolve_age_select(work,4097,0)==0 && xe_resolve_age_select(work,1,1)==0);
  for(mode=0;mode<8;++mode)for(n=1;n<=4096;n=n<128?n+1:n*2) {
    for(i=0;i<n;++i)original[i]=mode==0?i:mode==1?n-i:mode==2?9:mode==3?i%7:
      mode==4?UINT32_MAX-i:mode==5?(i&1?UINT32_MAX:0):mode==6?(i<n/2?i:n-i):random_u32();
    memcpy(sorted,original,n*sizeof(*sorted));qsort(sorted,n,sizeof(*sorted),compare_u32);
    for(rank=0;rank<n;rank+=n<32?1:n/7+1) {
      memcpy(work,original,n*sizeof(*work));CHECK(xe_resolve_age_select(work,n,rank)==sorted[rank]);++cases;
    }
    memcpy(work,original,n*sizeof(*work));xe_resolve_age_heapsort(work,n);
    CHECK(!memcmp(work,sorted,n*sizeof(*work)));
  }
  for(i=0;i<500;++i) {
    n=random_u32()%4096+1;rank=random_u32()%n;
    for(mode=0;mode<n;++mode)work[mode]=sorted[mode]=random_u32();
    qsort(sorted,n,sizeof(*sorted),compare_u32);CHECK(xe_resolve_age_select(work,n,rank)==sorted[rank]);++cases;
  }
  printf("Owner age selection: %lu exact unsigned ranks plus sorted/reverse/ties/high-bit/heapsort cases pass\n",(unsigned long)cases);
  return 0;
}
int main(void) {
  xe_resolve_owners *s=(xe_resolve_owners*)malloc(sizeof(*s));
  uint64_t keys[4096],candidate=0;
  xe_resolve_owner_state *borrow[4096];
  uint32_t i,used=0,bucket=XE_RESOLVE_OWNER_BUCKETS-3;
  CHECK(s);memset(s,0xa5,sizeof(*s));xe_resolve_owners_reset(s);CHECK(validate(s)==0);
  /* One wrapping collision cluster including a full half-table. */
  while(used<4096) {
    if(xe_resolve_owner_bucket(candidate)==bucket)keys[used++]=candidate;
    ++candidate;
  }
  for(i=0;i<4096;++i) {
    xe_resolve_owner_state zero;
    memset(&zero,0,sizeof(zero));borrow[i]=xe_resolve_owners_get(s,keys[i]);CHECK(borrow[i] && !memcmp(borrow[i],&zero,sizeof(zero)));
    borrow[i]->address=i*4096;borrow[i]->length=4096;borrow[i]->submission=UINT64_C(0xfedcba9800000000)+i;
  }
  CHECK(validate(s)==0 && s->count==4096);
  CHECK(!xe_resolve_owners_get(s,UINT64_MAX));
  CHECK(xe_resolve_owners_get(s,keys[17])==borrow[17]);
  for(i=0;i<4096;i+=2)xe_resolve_owners_erase(s,(uint32_t)(borrow[i]-s->rows));
  CHECK(validate(s)==0);
  for(i=1;i<4096;i+=2) {
    CHECK(xe_resolve_owners_find(s,keys[i])==borrow[i]);
    CHECK(borrow[i]->address==i*4096 && borrow[i]->submission==UINT64_C(0xfedcba9800000000)+i);
  }
  for(i=0;i<4096;i+=2)CHECK(xe_resolve_owners_get(s,keys[i]));
  CHECK(validate(s)==0);
  for(i=0;i<20000;++i) {
    uint32_t id=random_u32()%4096;
    if(s->occupied[id>>6]&(UINT64_C(1)<<(id&63)))xe_resolve_owners_erase(s,id);
    if((i&3)!=0) {
      uint64_t key=((uint64_t)random_u32()<<32)|random_u32();
      xe_resolve_owner_state *row=xe_resolve_owners_get(s,key);
      CHECK(row || s->count==4096);
    }
    if(i%1000==0)CHECK(validate(s)==0);
  }
  CHECK(validate(s)==0);xe_resolve_owners_reset(s);CHECK(validate(s)==0);
  CHECK(xe_resolve_owners_get(s,0) && xe_resolve_owners_get(s,UINT64_MAX));
  xe_resolve_owners_clear_rearm(s);CHECK(selection()==0);
  printf("Resolve owner C89: wrapping 4096-key collision cluster, stable borrowed rows, bounded full insert, backshift/reuse/free-list checks pass; storage=%lu bytes\n",(unsigned long)sizeof(*s));
  free(s);return 0;
}
