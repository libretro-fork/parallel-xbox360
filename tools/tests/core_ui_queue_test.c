/* UI ordering, bounded admission and cross-thread ownership; MSVC C89. */
#include <stdio.h>
#include <rthreads/rthreads.h>
#include <retro_atomic.h>
#include "core_ui_queue.h"
static xe_ui_queue *queue;
static unsigned opened, closed;
static retro_atomic_int_t done;
static void notified(void *context, int active) {
  (void)context;
  if (active) ++opened; else ++closed;
}
static void producer(void *context) {
  unsigned i;
  xe_ui_request *request;
  (void)context;
  for (i=0;i<2000;++i) {
    while (xe_ui_queue_begin(queue, &request) == 0) sthread_yield();
    xe_ui_queue_end(queue, request);
  }
  retro_atomic_fetch_add_int(&done, 1);
}
static int choice_test(void) {
  static const struct {
    const char *labels[3];
    unsigned count, active, expected;
  } cases[] = {
    {{"Connect", "Continue OFFLINE", NULL}, 2, 0, 1},
    {{"No", "Yes", NULL}, 2, 0, 1},
    {{"Cancel", "Confirm", "Back"}, 3, 2, 1},
    {{"Back", "No", NULL}, 2, 1, 1},
    {{"No", "Back", NULL}, 2, 99, 0},
    {{"Cancel", "OK", "Yes"}, 3, 2, 2},
    {{"Don't save", "Save", NULL}, 2, 0, 1},
    {{"Do Not Proceed", "Proceed", NULL}, 2, 0, 1},
    {{"", NULL, NULL}, 1, 0, 0}
  };
  xe_ui_choice choice;
  unsigned i,j;
  for(i=0;i<sizeof(cases)/sizeof(cases[0]);++i){
    xe_ui_choice_begin(&choice,cases[i].active);
    for(j=0;j<cases[i].count;++j)xe_ui_choice_add(&choice,cases[i].labels[j],j);
    if(xe_ui_choice_result(&choice,cases[i].count)!=cases[i].expected)return 0;
  }
  return 1;
}
int main(void) {
  xe_ui_request *request, *other;
  unsigned i;
  sthread_t *workers[4];
  if(!choice_test())return 13;
  queue=xe_ui_queue_new(); if(!queue)return 1;
  if(xe_ui_queue_begin(queue,&request)!=1 || !xe_ui_queue_active(queue) ||
      xe_ui_queue_begin(queue,&other)!=0)return 2;
  xe_ui_queue_end(queue,request);
  for(i=0;i<11;++i)xe_ui_queue_step(queue,60,notified,NULL);
  if(opened!=1||closed!=0||xe_ui_queue_active(queue))return 3;
  xe_ui_queue_step(queue,60,notified,NULL);
  if(closed!=1)return 4;
  for(i=0;i<64;++i){if(xe_ui_queue_begin(queue,&request)!=1)return 5;xe_ui_queue_end(queue,request);}
  if(xe_ui_queue_begin(queue,&request)!=0)return 6;
  xe_ui_queue_step(queue,60,notified,NULL);
  if(xe_ui_queue_begin(queue,&request)!=1)return 7;
  for(i=0;i<20;++i)xe_ui_queue_step(queue,60,notified,NULL);
  if(closed!=1)return 8;
  xe_ui_queue_end(queue,request);
  for(i=0;i<12;++i)xe_ui_queue_step(queue,60,notified,NULL);
  if(closed!=2)return 9;
  for(i=0;i<4;++i){workers[i]=sthread_create(producer,NULL);if(!workers[i])return 10;}
  while(retro_atomic_load_acquire_int(&done)!=4)xe_ui_queue_step(queue,60000.0/1001.0,notified,NULL);
  for(i=0;i<4;++i)sthread_join(workers[i]);
  for(i=0;i<15;++i)xe_ui_queue_step(queue,60,notified,NULL);
  if(opened!=closed||xe_ui_queue_active(queue))return 11;
  /* Cancel a request whose deferred callback never ran. */
  if(xe_ui_queue_begin(queue,&request)!=1)return 12;
  xe_ui_queue_free(queue);
  puts("PASS: frontend-time UI ordering, bounded undrained requests, 8000 cross-thread completions and cancellation");
  return 0;
}
