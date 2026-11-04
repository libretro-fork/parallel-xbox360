/* C89 socket registry retirement and worker-exit ordering. */
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include "libretro/core_socket_worker.h"
#include <retro_atomic.h>
#include <rthreads/retro_eventcount.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"FAIL line %u\n",(unsigned)__LINE__); exit(1); } } while(0)
struct state;
struct socket_record { xe_socket_link link; struct state *state; int cancelled; };
struct state {
   xe_socket_worker *worker;
   retro_eventcount_t wake;
   retro_atomic_int_t stop,exited;
   struct socket_record sockets[16];
   unsigned cancelled,retired;
};
static void run(void *opaque)
{
   struct state *state=(struct state*)opaque;
   int key;
   while(!retro_atomic_load_acquire_int(&state->stop)) {
      key=retro_eventcount_prepare_wait(&state->wake);
      if(retro_atomic_load_acquire_int(&state->stop))
         retro_eventcount_cancel_wait(&state->wake);
      else retro_eventcount_commit_wait(&state->wake,key);
   }
   xe_socket_worker_unregister(state->worker,&state->sockets[0].link);
   retro_atomic_store_release_int(&state->exited,1);
}
static void stop(void *opaque)
{
   struct state *state=(struct state*)opaque;
   retro_atomic_store_release_int(&state->stop,1);
   retro_eventcount_notify(&state->wake);
}
static void cancel(void *opaque)
{
   struct socket_record *socket=(struct socket_record*)opaque;
   CHECK(!socket->cancelled);
   socket->cancelled=1;
   ++socket->state->cancelled;
}
static void retire(void *opaque)
{
   struct socket_record *socket=(struct socket_record*)opaque;
   CHECK(socket->cancelled);
   CHECK(retro_atomic_load_acquire_int(&socket->state->exited));
   CHECK(!socket->link.linked);
   /* Retirement may release an object which unregisters recursively. */
   xe_socket_worker_unregister(socket->state->worker,&socket->link);
   ++socket->state->retired;
}
int main(void)
{
   struct state state;
   unsigned round,i;
   for(round=0;round<128;++round) {
      memset(&state,0,sizeof(state));
      CHECK(retro_eventcount_init(&state.wake));
      state.worker=xe_socket_worker_new(run,stop,&state); CHECK(state.worker);
      for(i=0;i<16;++i) {
         state.sockets[i].state=&state;
         CHECK(xe_socket_worker_register(state.worker,&state.sockets[i].link,
               &state.sockets[i],cancel,retire));
      }
      xe_socket_worker_stop(state.worker);
      CHECK(state.cancelled==16 && state.retired==15);
      CHECK(!xe_socket_worker_register(state.worker,&state.sockets[0].link,
            &state.sockets[0],cancel,retire));
      xe_socket_worker_stop(state.worker);
      CHECK(state.cancelled==16 && state.retired==15);
      xe_socket_worker_free(state.worker);
      retro_eventcount_free(&state.wake);
   }
   puts("Socket worker: 128 cancel/join/retire rounds and reentrant removal: PASS");
   return 0;
}