/* Borrowed source view: producer retains its bytes through reader release. C89. */
#ifndef XE_CORE_AUDIO_BORROW_H
#define XE_CORE_AUDIO_BORROW_H
#include <stddef.h>
#include <retro_inline.h>
#include <retro_atomic.h>
#include <rthreads/retro_eventcount.h>
enum { XE_AUDIO_LOAN_ACTIVE=1, XE_AUDIO_LOAN_READING=2, XE_AUDIO_LOAN_CANCELLED=4 };
typedef struct xe_audio_borrow {
   const float *samples;
   size_t frames,offset;
   unsigned channels;
   int sequential_be;
   float volume;
   retro_atomic_int_t state;
} xe_audio_borrow;
static INLINE int xe_audio_borrow_publish(xe_audio_borrow *view,const float *samples,
      size_t frames,unsigned channels,int sequential_be,float volume)
{
   if (retro_atomic_load_acquire_int(&view->state)) return 0;
   view->samples=samples;
   view->frames=frames;
   view->offset=0;
   view->channels=channels;
   view->sequential_be=sequential_be;
   view->volume=volume;
   return retro_atomic_cas_int(&view->state,0,XE_AUDIO_LOAN_ACTIVE);
}
static INLINE int xe_audio_borrow_read_begin(xe_audio_borrow *view)
{
   return retro_atomic_cas_int(&view->state,XE_AUDIO_LOAN_ACTIVE,
         XE_AUDIO_LOAN_ACTIVE|XE_AUDIO_LOAN_READING);
}
static INLINE void xe_audio_borrow_read_end(xe_audio_borrow *view,size_t frames)
{
   int clear=XE_AUDIO_LOAN_READING;
   view->offset+=frames;
   if (view->offset==view->frames) clear|=XE_AUDIO_LOAN_ACTIVE;
   /* No source or descriptor access after this release: its producer may
    * immediately reuse the borrowed storage when ACTIVE clears. */
   retro_atomic_fetch_and_int(&view->state,~clear);
}
static INLINE void xe_audio_borrow_cancel(xe_audio_borrow *view,retro_eventcount_t *changed)
{
   int key;
   retro_atomic_fetch_or_int(&view->state,XE_AUDIO_LOAN_CANCELLED);
   retro_eventcount_notify(changed);
   while (retro_atomic_load_acquire_int(&view->state)&XE_AUDIO_LOAN_READING) {
      key=retro_eventcount_prepare_wait(changed);
      if (!(retro_atomic_load_acquire_int(&view->state)&XE_AUDIO_LOAN_READING))
         retro_eventcount_cancel_wait(changed);
      else retro_eventcount_commit_wait(changed,key);
   }
   retro_atomic_fetch_and_int(&view->state,~XE_AUDIO_LOAN_ACTIVE);
   retro_eventcount_notify(changed);
}
#endif
