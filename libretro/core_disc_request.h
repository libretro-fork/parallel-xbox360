/* Caller-owned disc answer node; no C++ function wrapper or allocation. */
#ifndef XE_CORE_DISC_REQUEST_H
#define XE_CORE_DISC_REQUEST_H
#ifdef __cplusplus
extern "C" {
#endif
typedef void (*xe_disc_answer_fn)(void *, const char *);
typedef void (*xe_disc_notice_fn)(const char *);
typedef struct xe_disc_request {
   struct xe_disc_request *next;
   xe_disc_answer_fn answer;
   void *context;
   int pending;
} xe_disc_request;

void xe_disc_request_init(xe_disc_request *, xe_disc_answer_fn, void *);
#ifdef __cplusplus
}
#endif
#endif