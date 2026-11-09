/* Bounded multi-producer/single-consumer record leases. MSVC C89.
 * One reservation owns count consecutive units until consumer commit.
 * Producers write final payload storage, then publish once per record.
 * Consumer views are borrowed until advance; commit releases the whole batch.
 * Stop is terminal: join/quiesce every producer and consumer before free. */
#ifndef XE_CORE_MPSC_RECORDS_H
#define XE_CORE_MPSC_RECORDS_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xe_mpsc_records xe_mpsc_records;
xe_mpsc_records *xe_mpsc_records_new(size_t capacity);
void xe_mpsc_records_free(xe_mpsc_records *);
size_t xe_mpsc_records_bytes(const xe_mpsc_records *);
int xe_mpsc_records_claim(xe_mpsc_records *,size_t count,size_t *ticket);
void xe_mpsc_records_publish(xe_mpsc_records *,size_t ticket,size_t count);
void xe_mpsc_records_notify(xe_mpsc_records *);
void xe_mpsc_records_publish_notify(xe_mpsc_records *,size_t ticket,size_t count);
size_t xe_mpsc_records_peek(xe_mpsc_records *,size_t *ticket);
void xe_mpsc_records_advance(xe_mpsc_records *,size_t count);
void xe_mpsc_records_commit(xe_mpsc_records *);
int xe_mpsc_records_wait(xe_mpsc_records *);
void xe_mpsc_records_await(xe_mpsc_records *,size_t end);
void xe_mpsc_records_stop(xe_mpsc_records *);
#if defined(XE_MPSC_RECORDS_TEST)
/* Private wrap-fixture entrypoint, absent from the core build. */
void xe_mpsc_records_seed_empty(xe_mpsc_records *,size_t ticket);
#endif
#ifdef __cplusplus
}
#endif
#endif
