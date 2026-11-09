#ifndef BENCH_UNIT
#define BENCH_UNIT 16
#endif
#include "libretro/core_mpsc_records.h"
#include <disruptorplus/multi_threaded_claim_strategy.hpp>
#include <disruptorplus/spin_wait_strategy.hpp>
#include <disruptorplus/sequence_barrier.hpp>
#include <cstring>
namespace dp=disruptorplus;
struct old_queue {
 dp::spin_wait_strategy wait;
 dp::multi_threaded_claim_strategy<dp::spin_wait_strategy> claim;
 dp::sequence_barrier<dp::spin_wait_strategy> consumed;
 size_t read=0;dp::sequence_t known=dp::sequence_t(-1);
 unsigned char* bytes;size_t cap;
 old_queue(size_t n,unsigned char* b):claim(n,wait),consumed(wait),bytes(b),cap(n){claim.add_claim_barrier(consumed);}
};
struct wrapper {old_queue* old=nullptr;xe_mpsc_records* current=nullptr;};
extern "C" void* bench_new(unsigned current,size_t capacity,unsigned char* bytes) {
 auto* q=new wrapper;
 if(current)q->current=xe_mpsc_records_new(capacity);else q->old=new old_queue(capacity,bytes);
 return q;
}
extern "C" void bench_free(void* opaque){auto*q=static_cast<wrapper*>(opaque);delete q->old;xe_mpsc_records_free(q->current);delete q;}
extern "C" size_t bench_bytes(void* opaque){auto*q=static_cast<wrapper*>(opaque);return q->old?sizeof(old_queue)+q->old->cap*sizeof(std::atomic<dp::sequence_t>)+sizeof(void*):xe_mpsc_records_bytes(q->current);}
extern "C" size_t bench_claim(void* opaque,size_t n){auto*q=static_cast<wrapper*>(opaque);size_t ticket;if(q->old)return size_t(q->old->claim.claim(n).first());xe_mpsc_records_claim(q->current,n,&ticket);return ticket;}
extern "C" void bench_publish(void* opaque,size_t ticket,size_t n,unsigned wake){auto*q=static_cast<wrapper*>(opaque);if(q->old)q->old->claim.publish(dp::sequence_range(ticket,n));else{if(wake)xe_mpsc_records_publish_notify(q->current,ticket,n);else xe_mpsc_records_publish(q->current,ticket,n);}}
extern "C" size_t bench_peek(void* opaque,size_t* ticket){auto*q=static_cast<wrapper*>(opaque);if(!q->old)return xe_mpsc_records_peek(q->current,ticket);auto* o=q->old;
 if(dp::difference(o->known,dp::sequence_t(o->read))<0)o->known=o->claim.last_published_after(o->known);
 if(dp::difference(o->known,dp::sequence_t(o->read))<0)return 0;
 size_t n;std::memcpy(&n,o->bytes+(o->read&(o->cap-1))*BENCH_UNIT,sizeof(n));
 if(dp::difference(o->known,dp::sequence_t(o->read+n-1))<0)o->known=o->claim.last_published_after(o->known);
 if(dp::difference(o->known,dp::sequence_t(o->read+n-1))<0)return 0;
 *ticket=o->read;return n;}
extern "C" void bench_advance(void* opaque,size_t n){auto*q=static_cast<wrapper*>(opaque);if(q->old)q->old->read+=n;else xe_mpsc_records_advance(q->current,n);}
extern "C" void bench_commit(void* opaque){auto*q=static_cast<wrapper*>(opaque);if(q->old)q->old->consumed.publish(q->old->read-1);else xe_mpsc_records_commit(q->current);}
