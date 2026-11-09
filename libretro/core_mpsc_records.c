#include "core_mpsc_records.h"
#include <retro_atomic.h>
#include <rthreads/retro_eventcount.h>
#include <limits.h>
#include <stdlib.h>
struct xe_mpsc_records {
   size_t capacity;
   retro_atomic_int_t *ready;
   retro_eventcount_t work,space;
   retro_atomic_int_t stopped,work_waiters,space_waiters;
   unsigned char config_pad[128];
   retro_atomic_size_t next;
   unsigned char producer_pad[128];
   retro_atomic_size_t consumed;
   size_t read;
   unsigned char consumer_pad[128];
};
/* Permute, do not pad, the four-byte readiness words. Consecutive record
 * starts and common power-of-two record sizes should not all fight for the
 * same publication cache line. The high slot bits retain a bijection. */
static size_t ready_index(const xe_mpsc_records *q,size_t ticket)
{
   size_t slot=ticket&(q->capacity-1),bank;
   if(q->capacity<512)return slot;
   bank=(slot^(slot>>4)^(slot>>8))&15;
   return bank*(q->capacity>>4)+(slot>>4);
}
xe_mpsc_records *xe_mpsc_records_new(size_t capacity)
{
   xe_mpsc_records *q;
   size_t i;
   if(!capacity || (capacity&(capacity-1)) || capacity>INT_MAX ||
         capacity>((size_t)-1-sizeof(*q))/sizeof(retro_atomic_int_t))return NULL;
   q=(xe_mpsc_records*)calloc(1,sizeof(*q)+capacity*sizeof(retro_atomic_int_t));
   if(!q)return NULL;
   q->capacity=capacity;q->ready=(retro_atomic_int_t*)(q+1);
   for(i=0;i<capacity;++i)retro_atomic_int_init(q->ready+i,0);
   retro_atomic_size_init(&q->next,0);retro_atomic_size_init(&q->consumed,0);
   retro_atomic_int_init(&q->stopped,0);
   retro_atomic_int_init(&q->work_waiters,0);retro_atomic_int_init(&q->space_waiters,0);
   if(!retro_eventcount_init(&q->work) || !retro_eventcount_init(&q->space)) {
      retro_eventcount_free(&q->space);retro_eventcount_free(&q->work);free(q);return NULL;
   }
   return q;
}
void xe_mpsc_records_free(xe_mpsc_records *q)
{
   if(!q)return;
   retro_eventcount_free(&q->space);retro_eventcount_free(&q->work);free(q);
}
size_t xe_mpsc_records_bytes(const xe_mpsc_records *q)
{return q?sizeof(*q)+q->capacity*sizeof(*q->ready):0;}
int xe_mpsc_records_claim(xe_mpsc_records *q,size_t count,size_t *ticket)
{
   size_t start,end;int key;
   if(!q || !ticket || !count || count>q->capacity ||
         retro_atomic_load_acquire_int(&q->stopped))return 0;
   start=retro_atomic_fetch_add_size(&q->next,count);end=start+count;
   while(end-retro_atomic_load_acquire_size(&q->consumed)>q->capacity) {
      retro_atomic_fetch_add_seq_cst_int(&q->space_waiters,1);
      key=retro_eventcount_prepare_wait(&q->space);
      retro_atomic_thread_fence_seq_cst();
      if(end-retro_atomic_load_acquire_size(&q->consumed)<=q->capacity ||
            retro_atomic_load_acquire_int(&q->stopped))retro_eventcount_cancel_wait(&q->space);
      else retro_eventcount_commit_wait(&q->space,key);
      retro_atomic_fetch_add_int(&q->space_waiters,-1);
      if(retro_atomic_load_acquire_int(&q->stopped))return 0;
   }
   if(retro_atomic_load_acquire_int(&q->stopped))return 0;
   *ticket=start;return 1;
}
void xe_mpsc_records_publish(xe_mpsc_records *q,size_t ticket,size_t count)
{retro_atomic_store_release_int(q->ready+ready_index(q,ticket),(int)count);}
void xe_mpsc_records_publish_notify(xe_mpsc_records *q,size_t ticket,size_t count)
{
   /* Combine release publication and the store/load wake fence. The SC RMW
    * pairs with the consumer's registered-waiter/fence/recheck protocol. */
   (void)retro_atomic_exchange_int(q->ready+ready_index(q,ticket),(int)count);
   if(retro_atomic_load_acquire_int(&q->work_waiters))retro_eventcount_notify(&q->work);
}
void xe_mpsc_records_notify(xe_mpsc_records *q)
{
   /* Both sides order their store before the predicate recheck. Acq/rel alone
    * permits a missed wake on a store-buffering execution. */
   retro_atomic_thread_fence_seq_cst();
   if(retro_atomic_load_acquire_int(&q->work_waiters))retro_eventcount_notify(&q->work);
}
size_t xe_mpsc_records_peek(xe_mpsc_records *q,size_t *ticket)
{
   size_t count=(size_t)retro_atomic_load_acquire_int(q->ready+ready_index(q,q->read));
   if(count && ticket)*ticket=q->read;
   return count;
}
void xe_mpsc_records_advance(xe_mpsc_records *q,size_t count)
{
   retro_atomic_store_relaxed_int(q->ready+ready_index(q,q->read),0);
   q->read+=count;
}
void xe_mpsc_records_commit(xe_mpsc_records *q)
{
   retro_atomic_store_release_size(&q->consumed,q->read);
   retro_atomic_thread_fence_seq_cst();
   if(retro_atomic_load_acquire_int(&q->space_waiters))retro_eventcount_notify(&q->space);
}
int xe_mpsc_records_wait(xe_mpsc_records *q)
{
   int key;
   for(;;) {
      if(xe_mpsc_records_peek(q,NULL))return 1;
      if(retro_atomic_load_acquire_int(&q->stopped))return 0;
      retro_atomic_fetch_add_seq_cst_int(&q->work_waiters,1);
      key=retro_eventcount_prepare_wait(&q->work);
      retro_atomic_thread_fence_seq_cst();
      if(xe_mpsc_records_peek(q,NULL) || retro_atomic_load_acquire_int(&q->stopped))
         retro_eventcount_cancel_wait(&q->work);
      else retro_eventcount_commit_wait(&q->work,key);
      retro_atomic_fetch_add_int(&q->work_waiters,-1);
   }
}
void xe_mpsc_records_await(xe_mpsc_records *q,size_t end)
{
   int key;
   while(retro_atomic_load_acquire_size(&q->consumed)-end>(size_t)-1/2) {
      if(retro_atomic_load_acquire_int(&q->stopped))return;
      retro_atomic_fetch_add_seq_cst_int(&q->space_waiters,1);
      key=retro_eventcount_prepare_wait(&q->space);
      retro_atomic_thread_fence_seq_cst();
      if(retro_atomic_load_acquire_size(&q->consumed)-end<=(size_t)-1/2 ||
            retro_atomic_load_acquire_int(&q->stopped))retro_eventcount_cancel_wait(&q->space);
      else retro_eventcount_commit_wait(&q->space,key);
      retro_atomic_fetch_add_int(&q->space_waiters,-1);
   }
}
void xe_mpsc_records_stop(xe_mpsc_records *q)
{
   retro_atomic_store_release_int(&q->stopped,1);
   retro_eventcount_notify(&q->work);retro_eventcount_notify(&q->space);
}
#if defined(XE_MPSC_RECORDS_TEST)
void xe_mpsc_records_seed_empty(xe_mpsc_records *q,size_t ticket)
{retro_atomic_store_relaxed_size(&q->next,ticket);retro_atomic_store_relaxed_size(&q->consumed,ticket);q->read=ticket;}

#endif
