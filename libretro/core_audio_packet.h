/* One source packet, acknowledged after consumption. MSVC C89. */
#ifndef XE_CORE_AUDIO_PACKET_H
#define XE_CORE_AUDIO_PACKET_H
#include <stdint.h>
#include <stddef.h>
#include <retro_atomic.h>
#include <retro_inline.h>
#include <rthreads/retro_eventcount.h>
enum { XE_PACKET_PENDING=1, XE_PACKET_CALLING=2, XE_PACKET_CANCELLED=4, XE_PACKET_READY=8, XE_PACKET_REQUESTED=16 };
typedef struct xe_audio_packet {
   retro_atomic_size_t end, consumed;
   retro_atomic_int_t state, initialized;
   retro_eventcount_t idle;
   retro_eventcount_t *changed;
   void (*complete)(void *, int);
   void *context;
} xe_audio_packet;
static INLINE int xe_audio_packet_init(xe_audio_packet *packet,
      void (*complete)(void *, int), void *context)
{
   if (!retro_eventcount_init(&packet->idle)) return 0;
   retro_atomic_store_relaxed_int(&packet->state,XE_PACKET_READY);
   packet->complete=complete;
   packet->context=context;
   retro_atomic_store_release_int(&packet->initialized,1);
   return 1;
}
static INLINE void xe_audio_packet_ack(xe_audio_packet *packet)
{
   int state;
   size_t end, consumed;
   if (!retro_atomic_load_acquire_int(&packet->initialized)) return;
   for (;;) {
      state=retro_atomic_load_acquire_int(&packet->state);
      if (!(state & XE_PACKET_PENDING) ||
          (state & (XE_PACKET_CALLING|XE_PACKET_CANCELLED))) return;
      end=retro_atomic_load_relaxed_size(&packet->end);
      consumed=retro_atomic_load_acquire_size(&packet->consumed);
      if ((size_t)(consumed-end)>SIZE_MAX/2) return;
      if (!retro_atomic_cas_int(&packet->state,state,
            (state & ~XE_PACKET_PENDING)|XE_PACKET_CALLING)) continue;
      packet->complete(packet->context,1);
      retro_atomic_fetch_or_int(&packet->state,XE_PACKET_READY);
      retro_atomic_fetch_and_int(&packet->state,~XE_PACKET_CALLING);
      retro_eventcount_notify(&packet->idle);
      if (packet->changed) retro_eventcount_notify(packet->changed);
      /* A woken producer may have published the next packet meanwhile. */
   }
}
/* Consumption does not grant speculative source credit. Only the consumer
 * requests another packet when the current retro_run needs more samples. */
static INLINE void xe_audio_packet_request(xe_audio_packet *packet)
{
   int state, key;
   if (!retro_atomic_load_acquire_int(&packet->initialized)) return;
   for (;;) {
      state=retro_atomic_load_acquire_int(&packet->state);
      if ((state & XE_PACKET_CANCELLED) || !(state & XE_PACKET_READY)) return;
      if (state & XE_PACKET_CALLING) {
         key=retro_eventcount_prepare_wait(&packet->idle);
         if (!(retro_atomic_load_acquire_int(&packet->state)&XE_PACKET_CALLING))
            retro_eventcount_cancel_wait(&packet->idle);
         else retro_eventcount_commit_wait(&packet->idle,key);
         continue;
      }
      if (!retro_atomic_cas_int(&packet->state,state,
            (state & ~XE_PACKET_READY)|XE_PACKET_CALLING|XE_PACKET_REQUESTED))
         continue;
      packet->complete(packet->context,0);
      if (packet->changed) retro_eventcount_notify(packet->changed);
      retro_atomic_fetch_and_int(&packet->state,~XE_PACKET_CALLING);
      retro_eventcount_notify(&packet->idle);
      xe_audio_packet_ack(packet);
      return;
   }
}
static INLINE int xe_audio_packet_publish(xe_audio_packet *packet,size_t end)
{
   int state, next, key;
   if (!retro_atomic_load_acquire_int(&packet->initialized)) return 1;
   for (;;) {
      state=retro_atomic_load_acquire_int(&packet->state);
      if (state & XE_PACKET_CANCELLED) return 0;
      if (!(state & XE_PACKET_PENDING)) break;
      key=retro_eventcount_prepare_wait(&packet->idle);
      if (!(retro_atomic_load_acquire_int(&packet->state)&XE_PACKET_PENDING) ||
          (retro_atomic_load_acquire_int(&packet->state)&XE_PACKET_CANCELLED))
         retro_eventcount_cancel_wait(&packet->idle);
      else retro_eventcount_commit_wait(&packet->idle,key);
   }
   retro_atomic_store_relaxed_size(&packet->end,end);
   for (;;) {
      state=retro_atomic_load_acquire_int(&packet->state);
      if (state & XE_PACKET_CANCELLED) return 0;
      next=(int)(((unsigned)state+32u)&0x7fffffe0u) |
            (state & XE_PACKET_CALLING) | XE_PACKET_PENDING;
      if (retro_atomic_cas_int(&packet->state,state,next)) break;
   }
   xe_audio_packet_ack(packet);
   return 1;
}
static INLINE void xe_audio_packet_consume(xe_audio_packet *packet,size_t frames)
{
   if (!retro_atomic_load_acquire_int(&packet->initialized)) return;
   /* Publication and consumption can cross. The RMW orders this update
    * before checking PENDING, preventing both sides from missing completion. */
   retro_atomic_fetch_add_size(&packet->consumed,frames);
   xe_audio_packet_ack(packet);
}
/* Call before destroying the callback context. Never from complete() itself. */
static INLINE void xe_audio_packet_cancel(xe_audio_packet *packet)
{
   int key;
   if (!retro_atomic_load_acquire_int(&packet->initialized)) return;
   retro_atomic_fetch_or_int(&packet->state,XE_PACKET_CANCELLED);
   retro_eventcount_notify(&packet->idle);
   while (retro_atomic_load_acquire_int(&packet->state)&XE_PACKET_CALLING) {
      key=retro_eventcount_prepare_wait(&packet->idle);
      if (!(retro_atomic_load_acquire_int(&packet->state)&XE_PACKET_CALLING))
         retro_eventcount_cancel_wait(&packet->idle);
      else retro_eventcount_commit_wait(&packet->idle,key);
   }
}
static INLINE void xe_audio_packet_free(xe_audio_packet *packet)
{
   if (!retro_atomic_load_acquire_int(&packet->initialized)) return;
   xe_audio_packet_cancel(packet);
   retro_eventcount_free(&packet->idle);
}
#endif