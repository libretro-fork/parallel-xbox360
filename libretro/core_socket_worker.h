#ifndef XE_LIBRETRO_CORE_SOCKET_WORKER_H
#define XE_LIBRETRO_CORE_SOCKET_WORKER_H
#include <retro_common_api.h>
typedef struct xe_socket_worker xe_socket_worker;
typedef struct xe_socket_link {
   struct xe_socket_link *previous, *next;
   void *object;
   void (*cancel)(void*);
   void (*retire)(void*);
   int linked;
} xe_socket_link;
RETRO_BEGIN_DECLS
xe_socket_worker *xe_socket_worker_new(void (*run)(void*), void (*stop)(void*), void *opaque);
int xe_socket_worker_register(xe_socket_worker *worker, xe_socket_link *link,
      void *object, void (*cancel)(void*), void (*retire)(void*));
void xe_socket_worker_unregister(xe_socket_worker *worker, xe_socket_link *link);
/* Single control thread, after guest users quiesce. Removal may race worker exit. */
void xe_socket_worker_stop(xe_socket_worker *worker);
void xe_socket_worker_free(xe_socket_worker *worker);
RETRO_END_DECLS
#endif