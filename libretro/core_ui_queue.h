/* Headless guest UI notifications, driven only by frontend progress. */
#ifndef XE_CORE_UI_QUEUE_H
#define XE_CORE_UI_QUEUE_H
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xe_ui_choice {
  unsigned active, offline, confirm, preferred;
} xe_ui_choice;
void xe_ui_choice_begin(xe_ui_choice *choice, unsigned active);
void xe_ui_choice_add(xe_ui_choice *choice, const char *text, unsigned index);
unsigned xe_ui_choice_result(const xe_ui_choice *choice, unsigned count);
typedef struct xe_ui_queue xe_ui_queue;
typedef struct xe_ui_request xe_ui_request;
xe_ui_queue *xe_ui_queue_new(void);
/* Stop all producers before destruction. No callbacks run during destruction. */
void xe_ui_queue_free(xe_ui_queue *queue);
/* 1 accepted, 0 busy/full, -1 allocation failure or unavailable queue. */
int xe_ui_queue_begin(xe_ui_queue *queue, xe_ui_request **request);
void xe_ui_queue_end(xe_ui_queue *queue, xe_ui_request *request);
int xe_ui_queue_active(const xe_ui_queue *queue);
/* Single frontend owner. Callback runs only here, never on a producer thread. */
void xe_ui_queue_step(xe_ui_queue *queue, double fps,
    void (*notify)(void *context, int active), void *context);
#ifdef __cplusplus
}
#endif
#endif
