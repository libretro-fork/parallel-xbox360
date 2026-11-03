/* Bounded MPSC UI completion handoff; MSVC C89. */
#include "core_ui_queue.h"
#include "core_audio_cadence.h"
#include <stdlib.h>
#include <stdint.h>
#include <queues/mpsc_stack.h>
#include <retro_spsc.h>
#define UI_REQUESTS 64
#define UI_DELAY_TICKS 9600u
struct xe_ui_event {
  mpsc_stack_node_t link;
  struct xe_ui_request *request;
  int opening;
};
struct xe_ui_request {
  struct xe_ui_event begin, end;
};
struct xe_ui_queue {
  mpsc_stack_t incoming;
  retro_spsc_t available;
  retro_atomic_int_t active;
  struct xe_ui_request requests[UI_REQUESTS];
  xe_audio_cadence cadence;
  uint64_t ticks, close_at;
  unsigned visible_count;
  int showing;
};
static unsigned fold_ascii(unsigned c) {
  return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
}
static int contains_ascii(const char *text, const char *word) {
  const char *a, *b;
  for (; *text; ++text) {
    a = text; b = word;
    while (*a && *b && fold_ascii((unsigned char)*a) == (unsigned char)*b) { ++a; ++b; }
    if (!*b) return 1;
  }
  return 0;
}
void xe_ui_choice_begin(xe_ui_choice *choice, unsigned active) {
  choice->active = active;
  choice->offline = choice->confirm = choice->preferred = ~0u;
}
void xe_ui_choice_add(xe_ui_choice *choice, const char *text, unsigned index) {
  int back;
  if (!text) text = "";
  back = contains_ascii(text, "cancel") || contains_ascii(text, "back") ||
      (fold_ascii((unsigned char)text[0]) == 'n' &&
       fold_ascii((unsigned char)text[1]) == 'o' && !text[2]) ||
      contains_ascii(text, "don't") || contains_ascii(text, "do not");
  if (choice->offline == ~0u && contains_ascii(text, "offline")) choice->offline = index;
  if (!back) {
    if (choice->confirm == ~0u) choice->confirm = index;
    if (index == choice->active) choice->preferred = index;
  }
}
unsigned xe_ui_choice_result(const xe_ui_choice *choice, unsigned count) {
  unsigned result = choice->offline != ~0u ? choice->offline :
      choice->preferred != ~0u ? choice->preferred :
      choice->confirm != ~0u ? choice->confirm : choice->active;
  return result < count ? result : 0;
}

xe_ui_queue *xe_ui_queue_new(void) {
  xe_ui_queue *queue;
  xe_ui_request *request;
  unsigned i;
  queue = (xe_ui_queue *)calloc(1, sizeof(*queue));
  if (!queue) return NULL;
  if (!retro_spsc_init(&queue->available, UI_REQUESTS * sizeof(request))) {
    free(queue); return NULL;
  }
  mpsc_stack_init(&queue->incoming);
  for (i = 0; i < UI_REQUESTS; ++i) {
    request = &queue->requests[i];
    request->begin.request = request; request->begin.opening = 1;
    request->end.request = request; request->end.opening = 0;
    retro_spsc_write(&queue->available, &request, sizeof(request));
  }
  return queue;
}
void xe_ui_queue_free(xe_ui_queue *queue) {
  if (!queue) return;
  retro_spsc_free(&queue->available);
  free(queue);
}
int xe_ui_queue_begin(xe_ui_queue *queue, xe_ui_request **request) {
  if (!request) return -1;
  *request = NULL;
  if (!queue) return -1;
  if (!retro_atomic_cas_int(&queue->active, 0, 1)) return 0;
  /* The active gate serializes producers' access to the SPSC consumer cursor,
   * including when a different guest thread takes the next request. */
  if (retro_spsc_read(&queue->available, request, sizeof(*request)) != sizeof(*request)) {
    retro_atomic_store_release_int(&queue->active, 0);
    return 0;
  }
  mpsc_stack_push(&queue->incoming, &(*request)->begin.link);
  return 1;
}
void xe_ui_queue_end(xe_ui_queue *queue, xe_ui_request *request) {
  if (!queue || !request) return;
  mpsc_stack_push(&queue->incoming, &request->end.link);
  retro_atomic_store_release_int(&queue->active, 0);
}
int xe_ui_queue_active(const xe_ui_queue *queue) {
  return queue && retro_atomic_load_acquire_int(&queue->active);
}
void xe_ui_queue_step(xe_ui_queue *queue, double fps,
    void (*notify)(void *, int), void *context) {
  mpsc_stack_node_t *node, *next;
  struct xe_ui_event *event;
  xe_ui_request *request;
  if (!queue) return;
  node = mpsc_stack_reverse(mpsc_stack_drain(&queue->incoming));
  while (node) {
    next = node->next;
    event = (struct xe_ui_event *)node;
    if (event->opening) {
      ++queue->visible_count;
      queue->close_at = 0;
      if (!queue->showing) {
        queue->showing = 1;
        if (notify) notify(context, 1);
      }
    } else {
      --queue->visible_count;
      if (!queue->visible_count) queue->close_at = queue->ticks + UI_DELAY_TICKS;
      request = event->request;
      retro_spsc_write(&queue->available, &request, sizeof(request));
    }
    node = next;
  }
  queue->ticks += xe_audio_cadence_next(&queue->cadence, 48000, fps);
  if (queue->showing && !queue->visible_count && queue->close_at &&
      queue->ticks >= queue->close_at) {
    queue->showing = 0;
    queue->close_at = 0;
    if (notify) notify(context, 0);
  }
}
