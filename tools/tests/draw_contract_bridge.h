#ifndef XE_TEST_DRAW_CONTRACT_BRIDGE_H
#define XE_TEST_DRAW_CONTRACT_BRIDGE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
struct draw_callbacks { void *owner; uintptr_t (*create)(void*,size_t); void (*store)(void*,size_t,uintptr_t); };
void *draw_queue_new(const struct draw_callbacks*);
void draw_queue_threads(void*,size_t);
int draw_queue_try(void*,size_t*,unsigned*);
void draw_queue_free(void*);
int draw_scope_unwind(void);
#ifdef __cplusplus
}
#endif
#endif
