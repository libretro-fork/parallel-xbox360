/* Stable owners, exact full-key collision handling and detached lifetime. C89. */
#include "libretro/core_pipeline_hash_index.h"
#include <stdio.h>
enum{NODES=4096,BUCKETS=65536};
typedef struct owner {xe_pipeline_hash_node node;unsigned key,guard;} owner;
typedef char owner_first_member[offsetof(owner,node)==0?1:-1];
static owner records[NODES];
static xe_pipeline_hash_node *buckets[BUCKETS],*other_buckets[8];
static unsigned char seen[NODES];
static uint64_t key_hash(unsigned key,unsigned mode)
{
 if(mode==1)return ((uint64_t)(key%83)<<32)|42;
 if(mode==2)return 0;
 return (uint64_t)key*UINT64_C(0x9e3779b97f4a7c15);
}
#define CHECK(x) do{if(!(x)){fprintf(stderr,"pipeline index line%u\n",(unsigned)__LINE__);return 1;}}while(0)
int main(void)
{
 static const size_t capacities[6]={1,2,8,256,16384,65536};
 xe_pipeline_hash_index index={0},other={0},before;
 xe_pipeline_hash_node *node;owner *value;size_t cursor,count,c;
 unsigned mode,i,j,hits,found,cases=0;
 CHECK(!xe_pipeline_hash_init(NULL,buckets,1));
 CHECK(!xe_pipeline_hash_init(&index,NULL,1));CHECK(!xe_pipeline_hash_init(&index,buckets,0));
 CHECK(!xe_pipeline_hash_init(&index,buckets,3));
 CHECK(!xe_pipeline_hash_init(&index,buckets,(size_t)1<<(sizeof(size_t)*8-1)));
 CHECK(!xe_pipeline_hash_first(NULL,0));CHECK(!xe_pipeline_hash_next(NULL));
 CHECK(!xe_pipeline_hash_insert(NULL,NULL,0));CHECK(!xe_pipeline_hash_drain(NULL,NULL));
 CHECK(xe_pipeline_hash_init(&other,other_buckets,8));
 for(c=0;c<6;++c)for(mode=0;mode<3;++mode){
  memset(records,0,sizeof(records));CHECK(xe_pipeline_hash_init(&index,buckets,capacities[c]));
  CHECK(!xe_pipeline_hash_first(&index,0));cursor=0;CHECK(!xe_pipeline_hash_drain(&index,&cursor));
  for(i=0;i<NODES;++i){
   records[i].key=i;records[i].guard=i^0xa5a5a5a5u;
   CHECK(xe_pipeline_hash_insert(&index,&records[i].node,key_hash(i,mode)));
   CHECK(index.count==i+1 && records[i].node.index==&index);
   CHECK(!xe_pipeline_hash_insert(&index,&records[i].node,UINT64_C(0xffff)));
   CHECK(!xe_pipeline_hash_insert(&other,&records[i].node,0));
  }
  before=index;CHECK(!xe_pipeline_hash_init(&index,other_buckets,8));CHECK(!memcmp(&before,&index,sizeof(index)));
  CHECK(other.count==0);
  for(i=0;i<NODES;++i){
   hits=found=0;
   for(node=xe_pipeline_hash_first(&index,key_hash(i,mode));node;node=xe_pipeline_hash_next(node)){
    CHECK(++hits<=NODES && node->index==&index && node->hash==key_hash(i,mode));
    value=(owner*)node;CHECK(value->key<NODES && value==records+value->key && value->guard==(value->key^0xa5a5a5a5u));
    if(value->key==i){CHECK(node==&records[i].node);++found;}
   }
   CHECK(found==1);++cases;
  }
  CHECK(!xe_pipeline_hash_first(&index,UINT64_C(0xffffffffffffffff)));
  memset(seen,0,sizeof(seen));cursor=0;count=0;
  while((node=xe_pipeline_hash_drain(&index,&cursor))!=NULL){
   value=(owner*)node;CHECK(value->key<NODES && !seen[value->key]);seen[value->key]=1;
   CHECK(!node->index && !node->next && value==records+value->key);
   CHECK(node->hash==key_hash(value->key,mode) && value->guard==(value->key^0xa5a5a5a5u));++count;
  }
  CHECK(count==NODES && !index.count);CHECK(!xe_pipeline_hash_drain(&index,&cursor));
  for(j=0;j<capacities[c];++j)CHECK(!buckets[j]);
  for(i=0;i<NODES;++i){CHECK(seen[i]);CHECK(xe_pipeline_hash_insert(&index,&records[i].node,key_hash(i,mode)));}
  cursor=0;count=0;while(xe_pipeline_hash_drain(&index,&cursor))++count;CHECK(count==NODES && !index.count);
 }
 CHECK(!xe_pipeline_hash_insert(&index,NULL,0));CHECK(!xe_pipeline_hash_drain(&index,NULL));
 index.count=(size_t)-1;CHECK(!xe_pipeline_hash_insert(&index,&records[0].node,0));index.count=0;
 printf("PASS: %u full-key lookups, six fixed bucket capacities, adversarial hash/bucket collisions, duplicate ownership and reusable detached drains\n",cases);
 return 0;
}
