/* Frontend requests, two native-quantum producers, consecutive mixed output. C89. */
#include "core_audio_queue.h"
#include <retro_atomic.h>
#include <rthreads/rthreads.h>
#include <rthreads/retro_eventcount.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); abort(); } } while (0)
typedef struct producer_job {
   xe_audio_queue *queue;
   unsigned channels, multiplier, quantum, media, borrowed;
   retro_atomic_size_t blocks;
   retro_atomic_int_t credit, stop;
   retro_eventcount_t changed;
} producer_job;
static float sample(size_t frame, unsigned channel, unsigned multiplier) {
   return (float)(((frame % 1024) * 6 + channel) * multiplier);
}
static void packet_event(void *opaque, int consumed) {
   producer_job *job=(producer_job *)opaque;
   if (!consumed && !job->media) {
      retro_atomic_store_release_int(&job->credit,1);
      retro_eventcount_notify(&job->changed);
   }
}
static void produce(void *opaque) {
   producer_job *job = (producer_job *)opaque;
   float data[768 * 6];
   size_t position = 0, i, blocks = 0;
   unsigned ch;
   int key;
   for (;;) {
      if (job->media && !xe_audio_queue_wait_request(job->queue)) return;
      while (!job->media && !retro_atomic_load_acquire_int(&job->credit)) {
         key=retro_eventcount_prepare_wait(&job->changed);
         if (retro_atomic_load_acquire_int(&job->credit) ||
             retro_atomic_load_acquire_int(&job->stop))
            retro_eventcount_cancel_wait(&job->changed);
         else retro_eventcount_commit_wait(&job->changed,key);
         if (retro_atomic_load_acquire_int(&job->stop)) return;
      }
      retro_atomic_store_release_int(&job->credit,0);
      for (i=0;i<job->quantum;++i) for (ch=0;ch<job->channels;++ch)
         data[i*job->channels+ch]=sample(position+i,ch,job->multiplier);
      retro_atomic_store_release_size(&job->blocks,++blocks);
      retro_eventcount_notify(&job->changed);
      if (job->borrowed) {
         if (xe_audio_queue_submit_borrowed(job->queue,data,job->quantum,job->channels,0,1.0f)!=job->quantum) break;
      } else {
         if (xe_audio_queue_write(job->queue,data,job->quantum*job->channels) != job->quantum*job->channels) break;
         if (!xe_audio_queue_finish_packet(job->queue)) break;
      }
      memset(data,0xFF,sizeof(data)); /* Reuse is legal only after release. */
      position+=job->quantum;
   }
}
static void wait_blocks(producer_job *job,size_t expected) {
   size_t got;
   int key;
   for (;;) {
      key=retro_eventcount_prepare_wait(&job->changed);
      got=retro_atomic_load_acquire_size(&job->blocks);
      if (got>=expected) { retro_eventcount_cancel_wait(&job->changed); CHECK(got==expected); return; }
      retro_eventcount_commit_wait(&job->changed,key);
   }
}
static void run(unsigned channels, unsigned media, unsigned borrowed) {
   xe_audio_registry *registry=xe_audio_registry_new(channels);
   xe_audio_group *group=xe_audio_group_new_frontend(registry);
   producer_job jobs[2];
   sthread_t *threads[2];
   float output[1024*6];
   size_t frame=0, frames, i, expected;
   unsigned stream, iteration,ch;
   CHECK(registry && group);
   memset(jobs,0,sizeof(jobs));
   for(stream=0;stream<2;++stream) {
      jobs[stream].media=media && stream==1;
      jobs[stream].quantum=jobs[stream].media?768:256;
      jobs[stream].borrowed=borrowed && (!jobs[stream].media || borrowed==2);
      jobs[stream].queue=jobs[stream].borrowed?
          xe_audio_group_queue_new_borrowed(group,jobs[stream].quantum,packet_event,&jobs[stream]):
          xe_audio_group_queue_new_packet(group,jobs[stream].quantum,packet_event,&jobs[stream]);
      jobs[stream].channels=channels;
      jobs[stream].multiplier=stream+1;
      CHECK(jobs[stream].queue && retro_eventcount_init(&jobs[stream].changed));
      CHECK(!retro_atomic_load_acquire_int(&jobs[stream].credit));
      threads[stream]=sthread_create(produce,&jobs[stream]);
      CHECK(threads[stream]);
   }
   for(iteration=0;iteration<1500;++iteration) {
      frames=iteration%3==0?800:(iteration%3==1?960:400);
      CHECK(xe_audio_registry_mix(registry,output,frames*channels)==frames*channels);
      for(i=0;i<frames;++i) for(ch=0;ch<channels;++ch)
         CHECK(output[i*channels+ch]==sample(frame+i,ch,1)+sample(frame+i,ch,2));
      frame+=frames;
      for(stream=0;stream<2;++stream) {
         expected=(frame+jobs[stream].quantum-1)/jobs[stream].quantum;
         wait_blocks(&jobs[stream],expected);
         CHECK(!retro_atomic_load_acquire_int(&jobs[stream].credit));
      }
   }
   xe_audio_group_stop(group);
   for(stream=0;stream<2;++stream) {
      retro_atomic_store_release_int(&jobs[stream].stop,1);
      retro_eventcount_notify(&jobs[stream].changed);
      sthread_join(threads[stream]);
      xe_audio_queue_free(jobs[stream].queue);
      retro_eventcount_free(&jobs[stream].changed);
   }
   xe_audio_group_free(group);
   xe_audio_registry_free(registry);
}
int main(void) {
   run(2,0,0); run(6,0,0); run(2,1,0); run(6,1,0);
   run(2,0,1); run(6,0,1); run(2,1,1); run(6,1,1);
   run(2,1,2); run(6,1,2);
   puts("PASS: 15000 frontend intervals, borrowed sources and mixed native/media packet sizes; exact consecutive mixed audio; no prefill/run-ahead beyond final native quantum; stop cancels waiting producers");
   return 0;
}