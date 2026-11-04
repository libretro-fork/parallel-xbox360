#include "core_socket_worker.h"
#include <rthreads/rthreads.h>
#include <assert.h>
#include <stdlib.h>
struct xe_socket_worker {
   slock_t *lock;
   sthread_t *thread;
   xe_socket_link *head;
   void (*run)(void*);
   void (*stop)(void*);
   void *opaque;
   int stopped;
};
static void worker_main(void *opaque)
{
   xe_socket_worker *worker=(xe_socket_worker*)opaque;
   sthread_setname("Xenia Socket I/O");
   worker->run(worker->opaque);
}
xe_socket_worker *xe_socket_worker_new(void (*run)(void*), void (*stop)(void*), void *opaque)
{
   xe_socket_worker *worker=(xe_socket_worker*)calloc(1,sizeof(*worker));
   if(!worker) return NULL;
   worker->lock=slock_new();
   if(!worker->lock) { free(worker); return NULL; }
   worker->run=run;
   worker->stop=stop;
   worker->opaque=opaque;
   worker->thread=sthread_create(worker_main,worker);
   if(!worker->thread) { slock_free(worker->lock); free(worker); return NULL; }
   return worker;
}
int xe_socket_worker_register(xe_socket_worker *worker, xe_socket_link *link,
      void *object, void (*cancel)(void*), void (*retire)(void*))
{
   slock_lock(worker->lock);
   if(worker->stopped) { slock_unlock(worker->lock); return 0; }
   link->object=object;
   link->cancel=cancel;
   link->retire=retire;
   link->previous=NULL;
   link->next=worker->head;
   if(worker->head) worker->head->previous=link;
   worker->head=link;
   link->linked=1;
   slock_unlock(worker->lock);
   return 1;
}
void xe_socket_worker_unregister(xe_socket_worker *worker, xe_socket_link *link)
{
   slock_lock(worker->lock);
   if(link->linked) {
      if(link->previous) link->previous->next=link->next;
      else worker->head=link->next;
      if(link->next) link->next->previous=link->previous;
      link->linked=0;
   }
   slock_unlock(worker->lock);
}
void xe_socket_worker_stop(xe_socket_worker *worker)
{
   xe_socket_link *link;
   if(!worker) return;
   slock_lock(worker->lock);
   if(worker->stopped) { slock_unlock(worker->lock); return; }
   worker->stopped=1;
   /* Destructors unregister before touching socket members. */
   for(link=worker->head;link;link=link->next) link->cancel(link->object);
   slock_unlock(worker->lock);
   worker->stop(worker->opaque);
   sthread_join(worker->thread);
   worker->thread=NULL;
   /* Guest users are quiescent and the I/O worker has joined. Detach before
    * retirement so releasing a guest event cannot recurse into this lock. */
   for(;;) {
      void *object;
      void (*retire)(void*);
      slock_lock(worker->lock);
      link=worker->head;
      if(!link) { slock_unlock(worker->lock); break; }
      worker->head=link->next;
      if(worker->head) worker->head->previous=NULL;
      link->linked=0;
      object=link->object;
      retire=link->retire;
      slock_unlock(worker->lock);
      retire(object);
   }
}
void xe_socket_worker_free(xe_socket_worker *worker)
{
   if(!worker) return;
   xe_socket_worker_stop(worker);
   assert(!worker->head);
   slock_free(worker->lock);
   free(worker);
}