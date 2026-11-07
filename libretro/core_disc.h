/* Streaming playlist metadata and caller-owned disc requests; MSVC C89. */
#ifndef XE_CORE_DISC_H
#define XE_CORE_DISC_H
#include <stddef.h>
#include "core_disc_request.h"
#include <stdint.h>
#include <streams/file_stream.h>
#include <rthreads/rthreads.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xe_disc_view { const char *data; size_t size; } xe_disc_view;
typedef struct xe_disc_lines {
   RFILE *file;
   char window[4097];
   char *spill;
   size_t used, at, spill_size, spill_capacity;
   unsigned line;
   int error;
} xe_disc_lines;
/* Zero initialize; views expire at the next read/close. No file-size slurp. */
int xe_disc_lines_open(xe_disc_lines *reader, const char *path);
int xe_disc_lines_next(xe_disc_lines *reader, xe_disc_view *view);
int xe_disc_lines_close(xe_disc_lines *reader);
/* Borrow a trimmed path; skip BOM on first physical line, blanks and comments. */
int xe_disc_line_path(xe_disc_view line, unsigned number, xe_disc_view *path);
char *xe_disc_join(const char *reference, const char *path);
xe_disc_view xe_disc_label(const char *path);
typedef struct xe_disc_paths { char **items; size_t count, capacity; } xe_disc_paths;
int xe_disc_paths_append(xe_disc_paths *paths, char *owned);
int xe_disc_paths_replace(xe_disc_paths *paths, size_t index, const char *path);
void xe_disc_paths_remove(xe_disc_paths *paths, size_t index);
void xe_disc_paths_close(xe_disc_paths *paths);
char *xe_disc_duplicate(const char *text);
char *xe_disc_text_retain(char *text);
void xe_disc_text_release(char *text);
typedef struct xe_disc_control {
   slock_t *lock;
   xe_disc_request *first, *last;
} xe_disc_control;
int xe_disc_control_init(xe_disc_control *control);
void xe_disc_control_close(xe_disc_control *control);
void xe_disc_control_lock(xe_disc_control *control);
void xe_disc_control_unlock(xe_disc_control *control);
void xe_disc_request_init(xe_disc_request *, xe_disc_answer_fn, void *);
/* Enqueue/detach under the control lock. Caller retains node until answer. */
int xe_disc_request_queue(xe_disc_control *, xe_disc_request *);
xe_disc_request *xe_disc_request_take(xe_disc_control *);
/* Outside lock: snapshot next/context before answering, since answer may retire
 * the caller's node immediately or reenter the control interface. */
void xe_disc_request_complete(xe_disc_request *, const char *);
int xe_disc_init(void);
void xe_disc_deinit(void);
void xe_disc_set_environment(retro_environment_t environment);
int xe_disc_load(const char *path, char *start, size_t capacity);
void xe_disc_unload(void);
void xe_disc_release(void);
void xe_disc_resume(void);
void xe_disc_set_notice(xe_disc_notice_fn notice);
void xe_disc_dispatch(uint8_t number, const char *message, xe_disc_request *request);
#ifdef __cplusplus
}
#endif
#endif
