/* Private support for the raw DXIL writer. MSVC-compatible C89. */
#include "dxil_support.h"

uint32_t fui(float value)
{
  uint32_t bits;
  memcpy(&bits, &value, sizeof(bits));
  return bits;
}

typedef struct Allocation {
  struct Allocation *parent;
  struct Allocation *first;
  struct Allocation *next;
  struct Allocation *prev;
  size_t size;
} Allocation;

void list_inithead(struct list_head *head) {
  head->next = head; head->prev = head;
}

void list_addtail(struct list_head *entry, struct list_head *head) {
  entry->next = head; entry->prev = head->prev;
  head->prev->next = entry; head->prev = entry;
}

unsigned list_length(const struct list_head *head) {
  const struct list_head *entry;
  unsigned count = 0;
  for (entry = head->next; entry != head; entry = entry->next) ++count;
  return count;
}

void *ralloc_size(const void *parent, size_t size) {
  Allocation *block;
  Allocation *owner;
  if (size > (size_t)-1 - sizeof(*block)) return NULL;
  block = (Allocation *)malloc(sizeof(*block) + (size ? size : 1));
  if (!block) return NULL;
  memset(block, 0, sizeof(*block));
  block->size = size;
  if (parent) {
    owner = (Allocation *)parent - 1;
    block->parent = owner;
    block->next = owner->first;
    if (block->next) block->next->prev = block;
    owner->first = block;
  }
  return block + 1;
}

void *rzalloc_size(const void *parent, size_t size) {
  void *result;
  result = ralloc_size(parent, size);
  if (result) memset(result, 0, size);
  return result;
}

void ralloc_free(void *pointer) {
  Allocation *block;
  if (!pointer) return;
  block = (Allocation *)pointer - 1;
  while (block->first) ralloc_free(block->first + 1);
  if (block->prev) block->prev->next = block->next;
  else if (block->parent) block->parent->first = block->next;
  if (block->next) block->next->prev = block->prev;
  free(block);
}

void *xe_dxil_array_alloc(const void *parent, size_t count, size_t size, int zero) {
  if (size && count > (size_t)-1 / size) return NULL;
  return zero ? rzalloc_size(parent, count * size) : ralloc_size(parent, count * size);
}

void *xe_dxil_array_realloc(const void *parent, void *old, size_t count, size_t size) {
  Allocation *previous;
  Allocation *replacement;
  Allocation *child;
  void *result;
  size_t copy_size;
  result = xe_dxil_array_alloc(parent, count, size, 0);
  if (!result || !old) return result;
  previous = (Allocation *)old - 1;
  replacement = (Allocation *)result - 1;
  copy_size = MIN2(previous->size, replacement->size);
  memcpy(result, old, copy_size);
  replacement->first = previous->first;
  previous->first = NULL;
  for (child = replacement->first; child; child = child->next) child->parent = replacement;
  ralloc_free(old);
  return result;
}

char *ralloc_strndup(const void *parent, const char *string, size_t count) {
  char *result;
  size_t size = 0;
  while (size < count && string[size]) ++size;
  if (size == (size_t)-1) return NULL;
  result = (char *)ralloc_size(parent, size + 1);
  if (result) { memcpy(result, string, size); result[size] = 0; }
  return result;
}

char *ralloc_strdup(const void *parent, const char *string) {
  return ralloc_strndup(parent, string, strlen(string));
}

void rb_tree_init(struct rb_tree *tree) { tree->root = NULL; }

const struct rb_node *rb_tree_search(const struct rb_tree *tree, const void *key,
  int (*compare)(const struct rb_node *, const void *)) {
  const struct rb_node *node;
  int order;
  for (node = tree->root; node;) {
    order = compare(node, key);
    if (!order) return node;
    node = order < 0 ? node->right : node->left;
  }
  return NULL;
}

void rb_tree_insert(struct rb_tree *tree, struct rb_node *node,
  int (*compare)(const struct rb_node *, const struct rb_node *)) {
  struct rb_node **location;
  location = &tree->root;
  while (*location) location = compare(node, *location) < 0 ? &(*location)->left : &(*location)->right;
  node->left = NULL; node->right = NULL; *location = node;
}

void blob_init(struct blob *blob) { memset(blob, 0, sizeof(*blob)); }

void blob_finish(struct blob *blob) {
  free(blob->data); memset(blob, 0, sizeof(*blob));
}

int blob_write_bytes(struct blob *blob, const void *data, size_t size) {
  size_t required;
  size_t capacity;
  unsigned char *replacement;
  if (blob->out_of_memory) return 0;
  if (size > (size_t)-1 - blob->size) { blob->out_of_memory = 1; return 0; }
  required = blob->size + size;
  if (required > blob->allocated) {
    capacity = blob->allocated ? blob->allocated : 4096;
    while (capacity < required) {
      if (capacity > (size_t)-1 / 2) { capacity = required; break; }
      capacity *= 2;
    }
    replacement = (unsigned char *)realloc(blob->data, capacity);
    if (!replacement) { blob->out_of_memory = 1; return 0; }
    blob->data = replacement; blob->allocated = capacity;
  }
  if (size) memcpy(blob->data + blob->size, data, size);
  blob->size = required;
  return 1;
}

intptr_t blob_reserve_uint32(struct blob *blob) {
  uint32_t zero = 0;
  size_t offset = blob->size;
  if (offset > (size_t)INTPTR_MAX || !blob_write_bytes(blob, &zero, sizeof(zero))) return -1;
  return (intptr_t)offset;
}

int blob_overwrite_uint32(struct blob *blob, size_t offset, uint32_t value) {
  if (offset > blob->size || blob->size - offset < sizeof(value)) return 0;
  memcpy(blob->data + offset, &value, sizeof(value));
  return 1;
}

struct _mesa_string_buffer *_mesa_string_buffer_create(const void *parent, uint32_t capacity) {
  struct _mesa_string_buffer *result;
  result = rzalloc(parent, struct _mesa_string_buffer);
  if (!result) return NULL;
  result->capacity = capacity ? capacity : 16;
  result->buf = ralloc_array(result, char, result->capacity);
  if (!result->buf) { ralloc_free(result); return NULL; }
  result->buf[0] = 0;
  return result;
}

int _mesa_string_buffer_append_len(struct _mesa_string_buffer *buffer, const char *data, uint32_t length) {
  uint32_t required;
  uint32_t capacity;
  char *replacement;
  if (length > UINT32_MAX - buffer->length - 1) return 0;
  required = buffer->length + length + 1;
  if (required > buffer->capacity) {
    capacity = buffer->capacity;
    while (capacity < required) {
      if (capacity > UINT32_MAX / 2) { capacity = required; break; }
      capacity *= 2;
    }
    replacement = reralloc(buffer, buffer->buf, char, capacity);
    if (!replacement) return 0;
    buffer->buf = replacement; buffer->capacity = capacity;
  }
  memcpy(buffer->buf + buffer->length, data, length);
  buffer->length += length; buffer->buf[buffer->length] = 0;
  return 1;
}

void _mesa_string_buffer_destroy(struct _mesa_string_buffer *buffer) { ralloc_free(buffer); }

unsigned util_logbase2(unsigned value) {
  unsigned result = 0;
  while (value >>= 1) ++result;
  return result;
}
