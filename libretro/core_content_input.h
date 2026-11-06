/* Synchronous borrowed input or one exact, uninitialized parser buffer. */
#ifndef XE_CORE_CONTENT_INPUT_H
#define XE_CORE_CONTENT_INPUT_H
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <retro_inline.h>
#ifndef XE_CONTENT_ALLOC
#define XE_CONTENT_ALLOC malloc
#define XE_CONTENT_FREE free
#endif
typedef struct xe_content_source {
  void *opaque;
  uint64_t size;
  const uint8_t *(*view)(void *opaque, uint64_t *available);
  int (*read)(void *opaque, uint64_t offset, void *out,
              size_t wanted, size_t *received);
} xe_content_source;
typedef struct xe_content_input {
  const uint8_t *data;
  size_t size;
  uint8_t *allocation;
} xe_content_input;
/* Zero-initialize before use. No copies of an acquired owner. Source/view must
 * outlive input use; release does not release the borrowed source. */
static INLINE void xe_content_input_release(xe_content_input *input) {
  if (!input) return;
  XE_CONTENT_FREE(input->allocation);
  input->data = NULL; input->size = 0; input->allocation = NULL;
}
/* 1 complete, 0 invalid/transfer failure, -1 allocation failure. Parser input
 * must be contiguous. Ordinary reads go straight into its final buffer, with
 * no zero-fill, growth, intermediate scratch or success on a partial prefix.
 * Publish allocation ownership before invoking callbacks, so a C++ caller's
 * scope guard can release it if an underlying virtual operation throws. */
static INLINE int xe_content_input_acquire(xe_content_input *input,
    const xe_content_source *source, size_t minimum) {
  uint64_t available = 0;
  const uint8_t *view;
  size_t offset, wanted, received, size;
  if (!input || input->data || input->allocation || !source ||
      !source->size || source->size < minimum || source->size > (size_t)-1)
    return 0;
  size = (size_t)source->size;
  if (source->view) {
    view = source->view(source->opaque, &available);
    if (view && available >= source->size) {
      input->data = view; input->size = size; return 1;
    }
  }
  if (!source->read) return 0;
  input->allocation = (uint8_t *)XE_CONTENT_ALLOC(size);
  if (!input->allocation) return -1;
  offset = 0;
  while (offset < size) {
    wanted = size - offset;
    if (wanted > 1048576) wanted = 1048576;
    received = 0;
    if (!source->read(source->opaque, offset, input->allocation + offset,
          wanted, &received) || !received || received > wanted) {
      xe_content_input_release(input); return 0;
    }
    offset += received;
  }
  input->data = input->allocation; input->size = size;
  return 1;
}
#endif
