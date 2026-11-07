/* Cold, finite bulk work. Callbacks borrow caller storage and must not throw
 * through C; all created workers join before run returns. MSVC C89. */
#ifndef XE_CORE_BULK_WORK_H
#define XE_CORE_BULK_WORK_H
#include <stddef.h>
#include <stdint.h>
#include <retro_atomic.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xe_bulk_work {
   retro_atomic_64_t next;
   unsigned char cursor_pad[128];
   retro_atomic_64_t completed;
   retro_atomic_int_t failed;
   size_t count;
} xe_bulk_work;
int xe_bulk_work_init(xe_bulk_work *work, size_t count);
int xe_bulk_work_claim(xe_bulk_work *work, size_t *index);
/* Only the first failing worker owns the error payload. Read it after join. */
int xe_bulk_work_fail(xe_bulk_work *work);
void xe_bulk_work_complete(xe_bulk_work *work, size_t count);
size_t xe_bulk_work_completed(const xe_bulk_work *work);
void xe_bulk_work_run(size_t background_workers, size_t jobs,
      void (*run)(void *), void *opaque, const char *name);
#ifdef __cplusplus
}
#endif
#endif
