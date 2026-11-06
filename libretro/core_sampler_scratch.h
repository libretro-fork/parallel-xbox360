/* CPU-only borrowed sampler handles. The texture cache owns their lifetime.
 * One command processor owns this scratch; zero-init, never shallow-copy it. */
#ifndef XE_CORE_SAMPLER_SCRATCH_H
#define XE_CORE_SAMPLER_SCRATCH_H
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <retro_inline.h>
#ifndef XE_SAMPLER_REALLOC
#define XE_SAMPLER_REALLOC realloc
#define XE_SAMPLER_FREE free
#endif
typedef struct xe_sampler_record { uint64_t sampler; uint32_t parameters; } xe_sampler_record;
typedef struct xe_sampler_buffer { xe_sampler_record *data; size_t count, capacity; } xe_sampler_buffer;
typedef struct xe_sampler_stage { xe_sampler_buffer buffers[2]; unsigned current, written; } xe_sampler_stage;
/* Always prepare the non-written slot. Failed growth preserves both snapshots.
 * Storage grows only past a high-water mark; there is no arbitrary shader cap. */
static INLINE int xe_sampler_begin(xe_sampler_stage *stage, size_t count) {
  xe_sampler_buffer *buffer;
  xe_sampler_record *data;
  size_t capacity;
  unsigned index;
  if (!stage || count > (size_t)-1 / sizeof(*data)) return 0;
  index = 1 - stage->written; buffer = stage->buffers + index;
  if (count > buffer->capacity) {
    capacity = buffer->capacity;
    if (capacity <= (size_t)-1 / (2 * sizeof(*data))) capacity *= 2;
    if (capacity < count) capacity = count;
    data = (xe_sampler_record *)XE_SAMPLER_REALLOC(buffer->data, capacity * sizeof(*data));
    if (!data) return 0;
    buffer->data = data; buffer->capacity = capacity;
  }
  buffer->count = count; stage->current = index; return 1;
}
static INLINE xe_sampler_buffer *xe_sampler_current(xe_sampler_stage *stage) {
  return stage->buffers + stage->current;
}
static INLINE int xe_sampler_matches(const xe_sampler_stage *stage) {
  const xe_sampler_buffer *a = stage->buffers + stage->current;
  const xe_sampler_buffer *b = stage->buffers + stage->written;
  size_t i;
  if (a == b) return 1;
  if (a->count != b->count) return 0;
  for (i = 0; i < a->count; ++i)
    if (a->data[i].parameters != b->data[i].parameters || a->data[i].sampler != b->data[i].sampler) return 0;
  return 1;
}
/* Commit only after successful descriptor preparation. Current remains stable
 * for repeated UpdateBindings. It now aliases written: records must stay
 * immutable until begin selects the other slot. Overflow updates precede commit.
 * No payload copy. */
static INLINE void xe_sampler_commit(xe_sampler_stage *stage) {
  stage->written = stage->current;
}
/* Zero descriptors may be intentional for a placeholder shader. Nonzero lists
 * must have been collected before async bindings became visible to this draw. */
static INLINE int xe_sampler_descriptor_count_valid(const xe_sampler_stage *stage, size_t count) {
  return !count || count == stage->buffers[stage->current].count;
}
static INLINE void xe_sampler_stage_free(xe_sampler_stage *stage) {
  unsigned i;
  for (i = 0; i < 2; ++i) {
    XE_SAMPLER_FREE(stage->buffers[i].data);
    stage->buffers[i].data = NULL;
    stage->buffers[i].count = stage->buffers[i].capacity = 0;
  }
  stage->current = stage->written = 0;
}
#endif
