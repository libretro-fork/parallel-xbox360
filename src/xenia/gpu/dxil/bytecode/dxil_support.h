/* Private allocation and container support for the raw DXIL writer. C89. */
#ifndef XE_GPU_DXIL_SUPPORT_H
#define XE_GPU_DXIL_SUPPORT_H
#include "dxil_namespace.h"
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <limits.h>
#ifdef _MSC_VER
#include <malloc.h>
#define inline __inline
#define alloca _alloca
#define snprintf _snprintf
#else
#ifdef __MINGW32__
#ifndef alloca
#define alloca __builtin_alloca
#endif
#else
#include <alloca.h>
#endif
#if !defined(__cplusplus) && (!defined(__STDC_VERSION__) || __STDC_VERSION__ < 199901L)
#define inline __inline__
#endif
#endif
#ifndef __cplusplus
typedef unsigned char bool;
#define true 1
#define false 0
#endif
#ifndef __func__
#define __func__ "dxil-writer"
#endif
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define MAX2(a,b) ((a) > (b) ? (a) : (b))
#define MIN2(a,b) ((a) < (b) ? (a) : (b))
#define DIV_ROUND_UP(a,b) (((a) + (b) - 1) / (b))
#define UNREACHABLE(message) abort()
#define container_of(pointer,type,member) ((type *)((char *)(pointer) - offsetof(type,member)))

struct list_head { struct list_head *next; struct list_head *prev; };
void list_inithead(struct list_head *head);
void list_addtail(struct list_head *entry, struct list_head *head);
unsigned list_length(const struct list_head *head);
#define LIST_FOR_EACH_ENTRY(type,entry,head,member) \
  for ((entry) = ((head)->next == (head) ? NULL : container_of((head)->next,type,member)); \
       (entry) != NULL; \
       (entry) = ((entry)->member.next == (head) ? NULL : container_of((entry)->member.next,type,member)))

void *ralloc_size(const void *parent, size_t size);
void *rzalloc_size(const void *parent, size_t size);
void *xe_dxil_array_alloc(const void *parent, size_t count, size_t size, int zero);
void *xe_dxil_array_realloc(const void *parent, void *old, size_t count, size_t size);
void ralloc_free(void *pointer);
char *ralloc_strdup(const void *parent, const char *string);
char *ralloc_strndup(const void *parent, const char *string, size_t count);
#define ralloc(parent,type) ((type *)ralloc_size((parent),sizeof(type)))
#define rzalloc(parent,type) ((type *)rzalloc_size((parent),sizeof(type)))
#define ralloc_array(parent,type,count) ((type *)xe_dxil_array_alloc((parent),(count),sizeof(type),0))
#define rzalloc_array(parent,type,count) ((type *)xe_dxil_array_alloc((parent),(count),sizeof(type),1))
#define reralloc(parent,old,type,count) ((type *)xe_dxil_array_realloc((parent),(old),(count),sizeof(type)))

struct rb_node { struct rb_node *left; struct rb_node *right; };
struct rb_tree { struct rb_node *root; };
void rb_tree_init(struct rb_tree *tree);
const struct rb_node *rb_tree_search(const struct rb_tree *tree, const void *key,
  int (*compare)(const struct rb_node *, const void *));
void rb_tree_insert(struct rb_tree *tree, struct rb_node *node,
  int (*compare)(const struct rb_node *, const struct rb_node *));

struct blob {
  unsigned char *data;
  size_t size;
  size_t allocated;
  int out_of_memory;
};
void blob_init(struct blob *blob);
void blob_finish(struct blob *blob);
int blob_write_bytes(struct blob *blob, const void *data, size_t size);
intptr_t blob_reserve_uint32(struct blob *blob);
int blob_overwrite_uint32(struct blob *blob, size_t offset, uint32_t value);

struct _mesa_string_buffer { char *buf; uint32_t length; uint32_t capacity; };
struct _mesa_string_buffer *_mesa_string_buffer_create(const void *parent, uint32_t capacity);
int _mesa_string_buffer_append_len(struct _mesa_string_buffer *buffer, const char *data, uint32_t length);
void _mesa_string_buffer_destroy(struct _mesa_string_buffer *buffer);
unsigned util_logbase2(unsigned value);
union di { double d; uint64_t ui; };
uint32_t fui(float value);
#endif
