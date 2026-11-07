/* Fixed callback lifetime and render credits. MSVC C89, common lock-free atomics. */
#ifndef XE_CORE_APU_CLIENTS_H
#define XE_CORE_APU_CLIENTS_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum {XE_APU_CLIENT_COUNT=8, XE_APU_RETIRE_NONE=0, XE_APU_RETIRE_OWN=1, XE_APU_RETIRE_WAIT=2};
typedef struct xe_apu_clients xe_apu_clients;
/* Ephemeral host association, never serialized. Copy into the driver; an old
 * generation cannot return credits to a reused slot. Table outlives drivers. */
typedef struct xe_apu_credit_sink {
  xe_apu_clients *owner;
  uint64_t token;
  unsigned index;
} xe_apu_credit_sink;
xe_apu_clients *xe_apu_clients_new(unsigned max_credits);
/* All callback/driver owners and dependency waiters must be quiescent. */
void xe_apu_clients_free(xe_apu_clients *table);
int xe_apu_clients_reserve(xe_apu_clients *table,unsigned *index,uint64_t *token);
int xe_apu_clients_publish(xe_apu_clients *table,unsigned index,uint64_t token,unsigned credits);
int xe_apu_clients_abandon(xe_apu_clients *table,unsigned index,uint64_t token);
int xe_apu_clients_active(const xe_apu_clients *table,unsigned index,uint64_t *token);
/* One callback worker. Acquire consumes exactly one credit with its lease.
 * Release returns 1 if retired resources are now ready for a cold collector,
 * 0 if live, -1 for a stale/invalid lease. No callback-side cleanup is required.
 * Claim ownership before cleanup; finish only after the resources are released. */
int xe_apu_clients_acquire(xe_apu_clients *table,unsigned index,uint64_t *token);
int xe_apu_clients_release(xe_apu_clients *table,unsigned index,uint64_t token);
int xe_apu_clients_retire(xe_apu_clients *table,unsigned index,uint64_t *token);
int xe_apu_clients_claim_cleanup(xe_apu_clients *table,unsigned index,uint64_t token);
int xe_apu_clients_collect(xe_apu_clients *table,unsigned index,uint64_t *token);
int xe_apu_clients_finish(xe_apu_clients *table,unsigned index,uint64_t token);
int xe_apu_clients_return_credit(const xe_apu_credit_sink *sink);
int xe_apu_clients_credits(const xe_apu_clients *table,unsigned index,uint64_t token,unsigned *credits);
/* Completion follows generation, so immediate reuse cannot strand an old waiter.
 * timeout 0 polls without reading time; -1 is a notification-driven dependency
 * wait; positive bounds are for control/tests, not emulated audio timing. */
int xe_apu_clients_wait(xe_apu_clients *table,unsigned index,uint64_t token,int64_t timeout_us);
/* After retirement, wait only for borrowed callback use to end. Another cold
 * caller may own reclamation; waiting for its heap work while holding a guest
 * critical region could deadlock. No slot reuse occurs until finish. */
int xe_apu_clients_wait_callback(xe_apu_clients *table,unsigned index,uint64_t token,int64_t timeout_us);
#ifdef XE_APU_CLIENT_TEST
int xe_apu_clients_test_seed(xe_apu_clients *table,unsigned index,uint64_t token);
#endif
#ifdef __cplusplus
}
#endif
#endif
