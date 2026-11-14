/* Intrusive shader-variant index. BSD license. MSVC C89.
 * The owner serializes mutation. Nodes and keys retain their identity while
 * indexed; readers must finish before removal/destruction. No allocation,
 * hashing, payload moves, or secondary node owner. Tree depth is logarithmic. */
#ifndef XE_CORE_SHADER_VARIANT_INDEX_H
#define XE_CORE_SHADER_VARIANT_INDEX_H
#include <stdint.h>
#include "../third_party/libretro-common/include/retro_inline.h"
typedef struct xe_shader_variant_node {
 struct xe_shader_variant_node *left,*right;
 uint64_t key;
 unsigned height;
} xe_shader_variant_node;
static INLINE unsigned xe_shader_variant_height(const xe_shader_variant_node *n) {
 return n?n->height:0;
}
static INLINE void xe_shader_variant_update(xe_shader_variant_node *n) {
 unsigned a=xe_shader_variant_height(n->left),b=xe_shader_variant_height(n->right);
 n->height=1+(a>b?a:b);
}
static INLINE xe_shader_variant_node *xe_shader_variant_rotate_left(xe_shader_variant_node *n) {
 xe_shader_variant_node *r=n->right;n->right=r->left;r->left=n;
 xe_shader_variant_update(n);xe_shader_variant_update(r);return r;
}
static INLINE xe_shader_variant_node *xe_shader_variant_rotate_right(xe_shader_variant_node *n) {
 xe_shader_variant_node *l=n->left;n->left=l->right;l->right=n;
 xe_shader_variant_update(n);xe_shader_variant_update(l);return l;
}
static INLINE xe_shader_variant_node *xe_shader_variant_balance(xe_shader_variant_node *n) {
 unsigned a,b;if(!n)return n;
 a=xe_shader_variant_height(n->left);b=xe_shader_variant_height(n->right);
 if(a>b+1){
  if(xe_shader_variant_height(n->left->right)>xe_shader_variant_height(n->left->left))
   n->left=xe_shader_variant_rotate_left(n->left);
  return xe_shader_variant_rotate_right(n);
 }
 if(b>a+1){
  if(xe_shader_variant_height(n->right->left)>xe_shader_variant_height(n->right->right))
   n->right=xe_shader_variant_rotate_right(n->right);
  return xe_shader_variant_rotate_left(n);
 }
 xe_shader_variant_update(n);return n;
}
static INLINE xe_shader_variant_node *xe_shader_variant_find(xe_shader_variant_node *root,uint64_t key) {
 /* Four levels keep predictable direction branches and amortize loop backs.
  * Computing a dependent child-array address delayed every MSVC node load. */
 while(root){
  if(root->key==key)return root;
  root=key<root->key?root->left:root->right;
  if(!root || root->key==key)return root;
  root=key<root->key?root->left:root->right;
  if(!root || root->key==key)return root;
  root=key<root->key?root->left:root->right;
  if(!root || root->key==key)return root;
  root=key<root->key?root->left:root->right;
 }
 return 0;
}
static INLINE xe_shader_variant_node *xe_shader_variant_insert_at(xe_shader_variant_node *root,
 xe_shader_variant_node *node,xe_shader_variant_node **found) {
 if(!root){node->left=0;node->right=0;node->height=1;*found=node;return node;}
 if(node->key==root->key){*found=root;return root;}
 if(node->key<root->key)root->left=xe_shader_variant_insert_at(root->left,node,found);
 else root->right=xe_shader_variant_insert_at(root->right,node,found);
 return xe_shader_variant_balance(root);
}
/* Returns the registered node. An equal-key candidate remains unmodified. */
static INLINE xe_shader_variant_node *xe_shader_variant_insert(xe_shader_variant_node **root,
 xe_shader_variant_node *node) {
 xe_shader_variant_node *found=0;
 if(!root || !node)return 0;
 *root=xe_shader_variant_insert_at(*root,node,&found);return found;
}
static INLINE xe_shader_variant_node *xe_shader_variant_pop_first(xe_shader_variant_node *root,
 xe_shader_variant_node **first) {
 if(!root->left){*first=root;return root->right;}
 root->left=xe_shader_variant_pop_first(root->left,first);return xe_shader_variant_balance(root);
}
static INLINE xe_shader_variant_node *xe_shader_variant_remove_at(xe_shader_variant_node *root,
 uint64_t key,xe_shader_variant_node **removed) {
 xe_shader_variant_node *replacement=0;
 if(!root)return 0;
 if(key<root->key)root->left=xe_shader_variant_remove_at(root->left,key,removed);
 else if(key>root->key)root->right=xe_shader_variant_remove_at(root->right,key,removed);
 else{
  *removed=root;
  if(!root->left)return root->right;
  if(!root->right)return root->left;
  /* Transplant the successor node, never a key or published payload. */
  root->right=xe_shader_variant_pop_first(root->right,&replacement);
  replacement->left=root->left;replacement->right=root->right;root=replacement;
 }
 return xe_shader_variant_balance(root);
}
static INLINE xe_shader_variant_node *xe_shader_variant_remove(xe_shader_variant_node **root,uint64_t key) {
 xe_shader_variant_node *removed=0;if(!root)return 0;
 *root=xe_shader_variant_remove_at(*root,key,&removed);
 if(removed){removed->left=0;removed->right=0;removed->height=1;}
 return removed;
}
/* Only for a detached index during owner destruction. Rotations flatten the
 * tree without maintaining heights; all nodes are consumed in amortized O(n).
 * No reader may query this private drain tree. Keys/payloads never move. */
static INLINE xe_shader_variant_node *xe_shader_variant_drain_pop(xe_shader_variant_node **root) {
 xe_shader_variant_node *node,*left;if(!root || !*root)return 0;
 node=*root;
 while(node->left){
#ifdef XE_SHADER_VARIANT_DRAIN_ROTATION
  XE_SHADER_VARIANT_DRAIN_ROTATION();
#endif
  left=node->left;node->left=left->right;left->right=node;node=left;
 }
 *root=node->right;node->left=0;node->right=0;node->height=1;return node;
}
#endif
