/* Native pipeline publication, never a wire format. A draw pins the handle it
 * loaded, not the independently changing control flags. MSVC C89. */
#ifndef XE_CORE_PIPELINE_PUBLICATION_H
#define XE_CORE_PIPELINE_PUBLICATION_H
#include <stdint.h>
#include <retro_atomic.h>
#include <retro_inline.h>
enum {
  XE_PIPELINE_PLACEHOLDER=1, XE_PIPELINE_INTERPRETER=2,
  XE_PIPELINE_FAILED=4, XE_PIPELINE_PENDING=8
};
typedef struct xe_pipeline_publication {
  retro_atomic_64_t handle, placeholder;
  retro_atomic_int_t flags;
} xe_pipeline_publication;
typedef struct xe_pipeline_draw {
  uint64_t handle;
  int placeholder, interpreter;
} xe_pipeline_draw;
static INLINE void xe_pipeline_publication_init(xe_pipeline_publication *p) {
  retro_atomic_64_init(&p->handle,0);
  retro_atomic_64_init(&p->placeholder,0);
  retro_atomic_int_init(&p->flags,0);
}
static INLINE uint64_t xe_pipeline_handle_load(const xe_pipeline_publication *p) {
  return (uint64_t)retro_atomic_load_acquire_64(&p->handle);
}
static INLINE uint64_t xe_pipeline_placeholder_load(const xe_pipeline_publication *p) {
  return (uint64_t)retro_atomic_load_acquire_64(&p->placeholder);
}
static INLINE unsigned xe_pipeline_flags_load(const xe_pipeline_publication *p) {
  return (unsigned)retro_atomic_load_acquire_int(&p->flags);
}
static INLINE int xe_pipeline_flag_load(const xe_pipeline_publication *p,unsigned bit) {
  return (xe_pipeline_flags_load(p) & bit) != 0;
}
static INLINE int xe_pipeline_is_pending(const void *state) {
  return xe_pipeline_flag_load((const xe_pipeline_publication *)state, XE_PIPELINE_PENDING);
}
static INLINE void xe_pipeline_flag_store(xe_pipeline_publication *p,unsigned bit,int value) {
  if(value) retro_atomic_fetch_or_int(&p->flags,(int)bit);
  else retro_atomic_fetch_and_int(&p->flags,~(int)bit);
}
static INLINE void xe_pipeline_handle_store(xe_pipeline_publication *p,uint64_t handle) {
  retro_atomic_store_release_64(&p->handle,(int64_t)handle);
}
static INLINE void xe_pipeline_placeholder_store(xe_pipeline_publication *p,uint64_t handle) {
  retro_atomic_store_release_64(&p->placeholder,(int64_t)handle);
}
static INLINE uint64_t xe_pipeline_handle_exchange(xe_pipeline_publication *p,uint64_t handle) {
  return (uint64_t)retro_atomic_exchange_64(&p->handle,(int64_t)handle);
}
/* Failure is permission for a warm-up retry. Publish it with pending cleared,
 * as the last access by the old creator: it must not clear a new retry's bit. */
static INLINE void xe_pipeline_publish_failure(xe_pipeline_publication *p) {
  int old,next;
  do {
    old=retro_atomic_load_acquire_int(&p->flags);
    next=(old & ~(XE_PIPELINE_PENDING|XE_PIPELINE_PLACEHOLDER)) | XE_PIPELINE_FAILED;
  } while(!retro_atomic_cas_int(&p->flags,old,next));
}
/* Destination is unpublished; source and its handles must remain alive. Copy
 * loads the handle first so subsequent metadata observes its publication. */
static INLINE void xe_pipeline_publication_copy(xe_pipeline_publication *dst,
    const xe_pipeline_publication *src) {
  retro_atomic_64_init(&dst->handle,(int64_t)xe_pipeline_handle_load(src));
  retro_atomic_64_init(&dst->placeholder,(int64_t)xe_pipeline_placeholder_load(src));
  retro_atomic_int_init(&dst->flags,(int)xe_pipeline_flags_load(src));
}
static INLINE xe_pipeline_draw xe_pipeline_for_draw(const xe_pipeline_publication *p) {
  xe_pipeline_draw result;
  result.handle=xe_pipeline_handle_load(p);
  result.placeholder=result.handle && result.handle==xe_pipeline_placeholder_load(p);
  result.interpreter=result.placeholder && xe_pipeline_flag_load(p,XE_PIPELINE_INTERPRETER);
  return result;
}
#endif
