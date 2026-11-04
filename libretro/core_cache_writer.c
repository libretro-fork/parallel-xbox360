#include "core_cache_writer.h"
#include <retro_spsc.h>
#include <rthreads/retro_eventcount.h>
#include <rthreads/rthreads.h>
#include <stdlib.h>
#include <string.h>

typedef struct xe_cache_message {
   size_t size;
   unsigned kind;
} xe_cache_message;
struct xe_cache_writer {
   retro_spsc_t queue;
   retro_eventcount_t work, space;
   retro_atomic_int_t stopped;
   sthread_t *thread;
   size_t stride, max_record;
   xe_cache_writer_sink sink;
   void *opaque;
};
static void xe_cache_writer_run(void *opaque)
{
   xe_cache_writer *writer = (xe_cache_writer *)opaque;
   const void *view;
   const xe_cache_message *message;
   int key;
   sthread_setname("Shader Storage Writer");
   for (;;) {
      if (retro_spsc_read_begin(&writer->queue, &view) >= writer->stride) {
         message = (const xe_cache_message *)view;
         writer->sink(writer->opaque, message->kind,
               (const unsigned char *)view + sizeof(*message), message->size);
         retro_spsc_read_end(&writer->queue, writer->stride);
         retro_eventcount_notify(&writer->space);
         continue;
      }
      /* Stop only after the last published record, including flush barriers. */
      if (retro_atomic_load_acquire_int(&writer->stopped)) {
         if (!retro_spsc_read_avail(&writer->queue)) break;
         continue;
      }
      key = retro_eventcount_prepare_wait(&writer->work);
      if (retro_spsc_read_avail(&writer->queue) ||
            retro_atomic_load_acquire_int(&writer->stopped))
         retro_eventcount_cancel_wait(&writer->work);
      else retro_eventcount_commit_wait(&writer->work, key);
   }
}
xe_cache_writer *xe_cache_writer_new(size_t max_record, size_t slots,
      xe_cache_writer_sink sink, void *opaque)
{
   xe_cache_writer *writer;
   size_t stride = sizeof(xe_cache_message), count = 1;
   if (!sink || max_record > (size_t)-1 - sizeof(xe_cache_message) || !slots)
      return NULL;
   while (stride < sizeof(xe_cache_message) + max_record) {
      if (stride > (size_t)-1 / 2) return NULL;
      stride *= 2;
   }
   /* Power-of-two stride and slot count prevent split records at ring wrap. */
   while (count < slots) {
      if (count > (size_t)-1 / 2) return NULL;
      count *= 2;
   }
   if (stride > ((size_t)-1 / 2) / count) return NULL;
   writer = (xe_cache_writer *)calloc(1, sizeof(*writer));
   if (!writer) return NULL;
   if (!retro_spsc_init(&writer->queue, stride * count)) goto fail;
   if (!retro_eventcount_init(&writer->work)) goto fail;
   if (!retro_eventcount_init(&writer->space)) goto fail;
   retro_atomic_int_init(&writer->stopped, 0);
   writer->stride = stride;
   writer->max_record = max_record;
   writer->sink = sink;
   writer->opaque = opaque;
   writer->thread = sthread_create(xe_cache_writer_run, writer);
   if (!writer->thread) goto fail;
   return writer;
fail:
   retro_eventcount_free(&writer->space);
   retro_eventcount_free(&writer->work);
   retro_spsc_free(&writer->queue);
   free(writer);
   return NULL;
}
int xe_cache_writer_submit(xe_cache_writer *writer, unsigned kind,
      const void *data, size_t size)
{
   void *view;
   xe_cache_message *message;
   int key;
   if (!writer || size > writer->max_record || (!data && size)) return 0;
   for (;;) {
      if (retro_atomic_load_acquire_int(&writer->stopped)) return 0;
      if (retro_spsc_write_begin(&writer->queue, &view) >= writer->stride) break;
      key = retro_eventcount_prepare_wait(&writer->space);
      if (retro_spsc_write_avail(&writer->queue) >= writer->stride ||
            retro_atomic_load_acquire_int(&writer->stopped))
         retro_eventcount_cancel_wait(&writer->space);
      else retro_eventcount_commit_wait(&writer->space, key);
   }
   message = (xe_cache_message *)view;
   message->size = size;
   message->kind = kind;
   if (size) memcpy((unsigned char *)view + sizeof(*message), data, size);
   retro_spsc_write_end(&writer->queue, writer->stride);
   retro_eventcount_notify(&writer->work);
   return 1;
}
void xe_cache_writer_free(xe_cache_writer *writer)
{
   if (!writer) return;
   retro_atomic_store_release_int(&writer->stopped, 1);
   retro_eventcount_notify(&writer->work);
   sthread_join(writer->thread);
   retro_eventcount_free(&writer->space);
   retro_eventcount_free(&writer->work);
   retro_spsc_free(&writer->queue);
   free(writer);
}
