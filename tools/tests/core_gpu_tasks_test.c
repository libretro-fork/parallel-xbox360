/* Fixed MPSC ownership/order/close. MSVC C89, owned common primitives. */
#include "core_gpu_tasks.h"
#include <retro_atomic.h>
#include <rthreads/rthreads.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"line %u: %s\n",(unsigned)__LINE__,#x); exit(1); } } while(0)
#define PRODUCERS 4
#define COUNT 50000
xe_gpu_task_fn xe_test_tasks_throw_fn(void);
int xe_test_tasks_take_throw(xe_gpu_tasks*);
int xe_test_tasks_inline_throw(void);
static unsigned allocations,live;
static size_t queue_bytes;
static int fail_allocate;
static retro_atomic_int_t held,release_held;
static void *held_context;
void *xe_gpu_task_test_alloc(size_t count,size_t size) {
  void *p;
  ++allocations;queue_bytes=count*size;
  if(fail_allocate) return NULL;
  p=calloc(count,size);if(p) ++live;return p;
}
void xe_gpu_task_test_free(void *p) { if(p) { CHECK(live);--live;free(p); } }
void xe_gpu_task_test_held(void *p) {
  if(!held_context || p!=held_context) return;
  retro_atomic_store_release_int(&held,1);
  while(!retro_atomic_load_acquire_int(&release_held)) {}
}
typedef struct message { uint32_t producer,sequence,bytes,check; } message;
typedef struct state {
  xe_gpu_tasks *queue;
  unsigned seen[PRODUCERS],ran,canceled;
  retro_atomic_int_t wakes,done;
} state;
typedef struct producer { state *state; unsigned id,accepted; } producer;
static void wake(void *p) { retro_atomic_fetch_add_int(&((state*)p)->wakes,1); }
static void receive(void *p,const void *data,size_t size) {
  state *s=(state*)p;
  message m;
  size_t i;
  CHECK(size>=sizeof(m));memcpy(&m,data,sizeof(m));
  CHECK(m.producer<PRODUCERS && m.sequence==s->seen[m.producer]++);
  CHECK(m.bytes==size && m.check==(m.producer^m.sequence^UINT32_C(0x4a5b6c7d)));
  for(i=sizeof(m);i<size;++i) CHECK(((const unsigned char*)data)[i]==(unsigned char)(m.sequence+i));
}
static void run(void *p,const void *data,size_t n) { receive(p,data,n);++((state*)p)->ran; }
static void cancel(void *p,const void *data,size_t n) { receive(p,data,n);++((state*)p)->canceled; }
static int send(state *s,unsigned id,unsigned sequence,size_t bytes) {
  message m;
  unsigned char tail[XE_GPU_TASK_LARGE_BYTES];
  size_t i;
  int result;
  m.producer=id;m.sequence=sequence;m.bytes=(uint32_t)bytes;m.check=id^sequence^UINT32_C(0x4a5b6c7d);
  for(i=sizeof(m);i<bytes;++i) tail[i-sizeof(m)]=(unsigned char)(sequence+i);
  result=xe_gpu_tasks_submit(s->queue,run,cancel,s,&m,sizeof(m),tail,bytes-sizeof(m));
  /* The caller immediately overwrites its temporary metadata; queue owns it. */
  memset(&m,0xee,sizeof(m));memset(tail,0xee,bytes-sizeof(m));return result;
}
static void initialize(state *s) {
  memset(s,0,sizeof(*s));retro_atomic_int_init(&s->wakes,0);retro_atomic_int_init(&s->done,0);
  s->queue=xe_gpu_tasks_new(wake,s);CHECK(s->queue);
}
static void basic(void) {
  state s;
  xe_gpu_task_view view,stale;
  unsigned i,before;
  unsigned char byte=0;
  fail_allocate=1;CHECK(!xe_gpu_tasks_new(NULL,NULL));fail_allocate=0;
  initialize(&s);before=allocations;
  CHECK(!xe_gpu_tasks_owner(s.queue));xe_gpu_tasks_set_owner(s.queue,&s);
  CHECK(xe_gpu_tasks_owner(s.queue)==&s);xe_gpu_tasks_set_owner(s.queue,NULL);
  CHECK(!xe_gpu_tasks_owner(s.queue));
  CHECK(!xe_gpu_tasks_submit(s.queue,NULL,cancel,&s,NULL,0,NULL,0));
  CHECK(!xe_gpu_tasks_payload_valid(NULL,1,NULL,0));
  CHECK(!xe_gpu_tasks_payload_valid(&byte,SIZE_MAX,&byte,1));
  for(i=0;i<XE_GPU_TASK_SMALL_COUNT;++i) CHECK(send(&s,0,i,32));
  CHECK(!send(&s,0,i,32));
  for(i=0;i<XE_GPU_TASK_LARGE_COUNT;++i) CHECK(send(&s,1,i,XE_GPU_TASK_LARGE_BYTES));
  CHECK(!send(&s,1,i,XE_GPU_TASK_LARGE_BYTES));
  CHECK(xe_gpu_tasks_take(s.queue,&view));stale=view;
  view.task->run(view.task->context,view.task->data,view.task->size);
  CHECK(xe_gpu_tasks_finish(s.queue,&view));CHECK(!xe_gpu_tasks_finish(s.queue,&view));
  CHECK(send(&s,0,XE_GPU_TASK_SMALL_COUNT,32));
  while(xe_gpu_tasks_take(s.queue,&view)) {
    view.task->run(view.task->context,view.task->data,view.task->size);
    if(view.task==stale.task) CHECK(!xe_gpu_tasks_finish(s.queue,&stale));
    CHECK(xe_gpu_tasks_finish(s.queue,&view));
  }
  CHECK(s.seen[0]==65 && s.seen[1]==4 && s.ran==69);
  CHECK(allocations==before && retro_atomic_load_acquire_int(&s.wakes)==69);
  CHECK(xe_gpu_tasks_reopen(s.queue));
  CHECK(send(&s,0,65,32));CHECK(xe_gpu_tasks_take(s.queue,&view));
  xe_gpu_tasks_close(s.queue);CHECK(!xe_gpu_tasks_closed_ready(s.queue));
  CHECK(!xe_gpu_tasks_free(s.queue));CHECK(!xe_gpu_tasks_reopen(s.queue));
  view.task->cancel(view.task->context,view.task->data,view.task->size);CHECK(xe_gpu_tasks_finish(s.queue,&view));
  CHECK(xe_gpu_tasks_closed_ready(s.queue));CHECK(!send(&s,0,66,32));
  CHECK(xe_gpu_tasks_reopen(s.queue));CHECK(send(&s,0,66,32));xe_gpu_tasks_cancel_all(s.queue);
  CHECK(s.seen[0]==67 && s.canceled==2);CHECK(xe_gpu_tasks_free(s.queue));CHECK(!live);
  CHECK(xe_gpu_tasks_free(NULL));
}
static void inline_parts(void) {
  state s;
  message m={0,0,32,UINT32_C(0x4a5b6c7d)};
  unsigned char tail[16],payload[32];
  unsigned i,before=allocations;
  memset(&s,0,sizeof(s));
  for(i=0;i<sizeof(tail);++i) tail[i]=(unsigned char)(i+sizeof(m));
  CHECK(xe_gpu_tasks_copy_parts(payload,sizeof(payload),&m,sizeof(m),tail,sizeof(tail)));
  run(&s,payload,sizeof(payload));
  CHECK(s.ran==1 && s.seen[0]==1 && allocations==before);
  CHECK(!xe_gpu_tasks_copy_parts(payload,sizeof(payload),NULL,1,tail,16));
  CHECK(!xe_gpu_tasks_copy_parts(payload,sizeof(payload),&m,SIZE_MAX,tail,16));
}
static void produce(void *p) {
  producer *producer=(struct producer*)p;
  unsigned i;
  for(i=0;i<COUNT;++i) {
    while(!send(producer->state,producer->id,i,32)) {
      if(xe_gpu_tasks_closed(producer->state->queue)) goto done;
    }
    ++producer->accepted;
  }
done:
  retro_atomic_fetch_add_int(&producer->state->done,1);
}
static void concurrent(int close_early) {
  state s;
  producer producers[PRODUCERS];
  sthread_t *threads[PRODUCERS];
  xe_gpu_task_view view;
  unsigned i,total=0,before;
  initialize(&s);before=allocations;
  for(i=0;i<PRODUCERS;++i) {
    producers[i].state=&s;producers[i].id=i;producers[i].accepted=0;
    threads[i]=sthread_create(produce,&producers[i]);CHECK(threads[i]);
  }
  while(retro_atomic_load_acquire_int(&s.done)!=PRODUCERS || !xe_gpu_tasks_empty(s.queue)) {
    if(xe_gpu_tasks_take(s.queue,&view)) {
      if(close_early && s.ran>=1000) {
        xe_gpu_tasks_close(s.queue);
        view.task->cancel(view.task->context,view.task->data,view.task->size);
      } else view.task->run(view.task->context,view.task->data,view.task->size);
      CHECK(xe_gpu_tasks_finish(s.queue,&view));
    }
  }
  for(i=0;i<PRODUCERS;++i) { sthread_join(threads[i]);total+=producers[i].accepted; CHECK(s.seen[i]==producers[i].accepted); }
  xe_gpu_tasks_cancel_all(s.queue);CHECK(s.ran+s.canceled==total);
  CHECK(close_early || total==PRODUCERS*COUNT);
  CHECK(allocations==before);CHECK(retro_atomic_load_acquire_int(&s.wakes)==(int)total);
  CHECK(xe_gpu_tasks_free(s.queue));CHECK(!live);
}
static void held_publish(void *p) { state *s=(state*)p; CHECK(send(s,0,0,32)); }
static void close_publisher(void) {
  state s;
  sthread_t *thread;
  initialize(&s);held_context=&s;
  retro_atomic_int_init(&held,0);retro_atomic_int_init(&release_held,0);
  thread=sthread_create(held_publish,&s);CHECK(thread);
  while(!retro_atomic_load_acquire_int(&held)) {}
  xe_gpu_tasks_close(s.queue);
  CHECK(!xe_gpu_tasks_closed_ready(s.queue));CHECK(!xe_gpu_tasks_free(s.queue));
  CHECK(!send(&s,1,0,32));retro_atomic_store_release_int(&release_held,1);
  sthread_join(thread);held_context=NULL;xe_gpu_tasks_cancel_all(s.queue);
  CHECK(s.canceled==1 && !s.ran && xe_gpu_tasks_closed_ready(s.queue));
  CHECK(xe_gpu_tasks_free(s.queue));CHECK(!live);
}
static void exceptions(void) {
  state s;
  initialize(&s);
  CHECK(xe_gpu_tasks_submit(s.queue,xe_test_tasks_throw_fn(),NULL,NULL,NULL,0,NULL,0));
  CHECK(xe_test_tasks_take_throw(s.queue));CHECK(xe_gpu_tasks_empty(s.queue));
  CHECK(xe_test_tasks_inline_throw());
  CHECK(send(&s,0,0,32));xe_gpu_tasks_cancel_all(s.queue);CHECK(s.canceled==1);
  CHECK(xe_gpu_tasks_free(s.queue));CHECK(!live);
}
int main(void) {
  basic();inline_parts();exceptions();concurrent(0);concurrent(1);close_publisher();
  printf("PASS: fixed MPSC control queue, 200000 ordered concurrent tasks, bounded rejection, stale lease, close/publish/cancel/reopen; %lu cold bytes\n",(unsigned long)queue_bytes);
  return 0;
}
