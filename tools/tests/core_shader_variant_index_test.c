/* Stable identities and independent live-set oracle. MSVC C89. */
static unsigned drain_rotations;
#define XE_SHADER_VARIANT_DRAIN_ROTATION() (++drain_rotations)
#include "libretro/core_shader_variant_index.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"variant index line %u\n",(unsigned)__LINE__);exit(1);}}while(0)
enum {CAPACITY=512};
typedef struct item {xe_shader_variant_node node;unsigned slot,live;uint64_t payload;} item;
static item storage[CAPACITY];static unsigned permutations,observations;
static uint64_t key_for(unsigned i) {
 return i==CAPACITY-1?UINT64_MAX:UINT64_C(0x9e3779b97f4a7c15)*i;
}
static void reset(void) {
 unsigned i;memset(storage,0,sizeof(storage));
 for(i=0;i<CAPACITY;++i){storage[i].node.key=key_for(i);storage[i].slot=i;storage[i].payload=key_for(i)^UINT64_C(0xabcdef0123456789);}
}
static unsigned verify_node(xe_shader_variant_node *root,unsigned depth,unsigned *seen,
 unsigned has_low,uint64_t low,unsigned has_high,uint64_t high) {
 unsigned l,r;item *entry;if(!root)return 0;
 CHECK(depth<32);entry=(item*)root;CHECK(entry->slot<CAPACITY && entry==storage+entry->slot);
 CHECK(entry->live && !seen[entry->slot]);seen[entry->slot]=1;
 CHECK(root->key==key_for(entry->slot));CHECK(entry->payload==(root->key^UINT64_C(0xabcdef0123456789)));
 CHECK(!has_low || root->key>low);CHECK(!has_high || root->key<high);
 l=verify_node(root->left,depth+1,seen,has_low,low,1,root->key);
 r=verify_node(root->right,depth+1,seen,1,root->key,has_high,high);
 CHECK(l<=r+1 && r<=l+1 && root->height==1+(l>r?l:r));return root->height;
}
static void verify(xe_shader_variant_node *root) {
 unsigned seen[CAPACITY]={0},i;verify_node(root,0,seen,0,0,0,0);
 for(i=0;i<CAPACITY;++i){
  CHECK(seen[i]==storage[i].live);
  CHECK(xe_shader_variant_find(root,key_for(i))==(storage[i].live?&storage[i].node:NULL));
 }
 ++observations;
}
static void insert(xe_shader_variant_node **root,unsigned i) {
 CHECK(xe_shader_variant_insert(root,&storage[i].node)==&storage[i].node);storage[i].live=1;
}
static void remove_item(xe_shader_variant_node **root,unsigned i) {
 xe_shader_variant_node *node=xe_shader_variant_remove(root,key_for(i));
 CHECK(node==(storage[i].live?&storage[i].node:NULL));storage[i].live=0;
 if(node)CHECK(!node->left && !node->right && node->height==1 && node->key==key_for(i));
}
static void permutation(unsigned *order,unsigned at) {
 unsigned i,t;xe_shader_variant_node *root=NULL;
 if(at<7){for(i=at;i<7;++i){t=order[i];order[i]=order[at];order[at]=t;
  permutation(order,at+1);t=order[i];order[i]=order[at];order[at]=t;}return;}
 reset();for(i=0;i<7;++i){insert(&root,order[i]);verify(root);}
 /* Repeated two-child root removal transplants nodes, preserving all payloads. */
 while(root){item *entry=(item*)root;remove_item(&root,entry->slot);verify(root);}
 ++permutations;
}
int main(void) {
 unsigned order[7]={0,1,2,3,4,5,6},i,selected;uint32_t random=123456789;
 xe_shader_variant_node *root=NULL,duplicate;permutation(order,0);CHECK(permutations==5040);
 reset();CHECK(!xe_shader_variant_find(NULL,0));CHECK(!xe_shader_variant_insert(NULL,&storage[0].node));
 CHECK(!xe_shader_variant_insert(&root,NULL));CHECK(!xe_shader_variant_remove(NULL,0));
 for(i=0;i<CAPACITY;++i)insert(&root,i);
 verify(root);
 for(i=0;i<CAPACITY;++i){
  memset(&duplicate,0,sizeof(duplicate));duplicate.key=key_for(i);duplicate.left=&duplicate;duplicate.height=123;
  CHECK(xe_shader_variant_insert(&root,&duplicate)==&storage[i].node);
  CHECK(duplicate.left==&duplicate && !duplicate.right && duplicate.height==123 && duplicate.key==key_for(i));
  CHECK(xe_shader_variant_insert(&root,&storage[i].node)==&storage[i].node);
 }
 verify(root);
 for(i=0;i<32768;++i){
  random=random*1664525u+1013904223u;selected=(random>>12)%CAPACITY;
  if(random&1)insert(&root,selected);else remove_item(&root,selected);
  verify(root);
 }
 for(i=0;i<CAPACITY;++i)remove_item(&root,i);
 verify(root);CHECK(!root);
 for(i=0;i<CAPACITY;++i)insert(&root,i);
 for(i=0;i<CAPACITY;++i){
  item *entry;xe_shader_variant_node *node=xe_shader_variant_drain_pop(&root);
  CHECK(node && !node->left && !node->right && node->height==1);
  entry=(item*)node;CHECK(entry->slot<CAPACITY && entry==storage+entry->slot && entry->live);
  CHECK(entry->payload==(key_for(entry->slot)^UINT64_C(0xabcdef0123456789)));
  entry->live=0;
 }
 CHECK(!root && !xe_shader_variant_drain_pop(&root) && !xe_shader_variant_drain_pop(NULL));
 for(i=0;i<CAPACITY;++i)CHECK(!storage[i].live);
 CHECK(drain_rotations>0 && drain_rotations<CAPACITY);
 printf("LINEAR_DRAIN nodes=512 stable_payloads=512 rotations=%u\n",drain_rotations);
 printf("PASS: %u permutations, %u live-set/identity/balance observations; full64-bit keys, duplicate ownership, transplant removal\n",permutations,observations);return 0;
}
