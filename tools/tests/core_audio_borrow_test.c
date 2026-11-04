/* Cancellation must fence a live borrowed reader. MSVC C89. */
#include "core_audio_borrow.h"
#include <rthreads/rthreads.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d\n",__LINE__); abort(); } } while(0)
typedef struct cancel_job {
   xe_audio_borrow view;
   retro_eventcount_t changed;
   retro_atomic_int_t returned;
} cancel_job;
static void cancel(void *opaque) {
   cancel_job *job=(cancel_job*)opaque;
   xe_audio_borrow_cancel(&job->view,&job->changed);
   retro_atomic_store_release_int(&job->returned,1);
   retro_eventcount_notify(&job->changed);
}
int main(void) {
   cancel_job job={0};
   float samples[256];
   sthread_t *thread;
   int key;
   unsigned i;
   for(i=0;i<256;++i) samples[i]=(float)i;
   CHECK(retro_eventcount_init(&job.changed));
   CHECK(xe_audio_borrow_publish(&job.view,samples,256,1,0,1.0f));
   CHECK(xe_audio_borrow_read_begin(&job.view));
   CHECK(job.view.samples==samples && job.view.frames==256);
   xe_audio_borrow_read_end(&job.view,32);
   CHECK(retro_atomic_load_acquire_int(&job.view.state)==XE_AUDIO_LOAN_ACTIVE);
   CHECK(job.view.offset==32);
   CHECK(xe_audio_borrow_read_begin(&job.view));
   thread=sthread_create(cancel,&job);CHECK(thread);
   /* The cancel flag is published before waiting for this live reader. */
   while (!(retro_atomic_load_acquire_int(&job.view.state)&XE_AUDIO_LOAN_CANCELLED)) {
      key=retro_eventcount_prepare_wait(&job.changed);
      if (retro_atomic_load_acquire_int(&job.view.state)&XE_AUDIO_LOAN_CANCELLED)
         retro_eventcount_cancel_wait(&job.changed);
      else retro_eventcount_commit_wait(&job.changed,key);
   }
   CHECK(!retro_atomic_load_acquire_int(&job.returned));
   CHECK(job.view.samples[32]==32.0f && job.view.samples[255]==255.0f);
   xe_audio_borrow_read_end(&job.view,224);
   retro_eventcount_notify(&job.changed);
   sthread_join(thread);
   CHECK(retro_atomic_load_acquire_int(&job.returned));
   CHECK(!xe_audio_borrow_read_begin(&job.view));
   CHECK(!xe_audio_borrow_publish(&job.view,samples,256,1,0,1.0f));
   retro_eventcount_free(&job.changed);
   puts("PASS: borrowed view identity, partial scope, cancellation fences readers and cancelled views reject reuse");
   return 0;
}
