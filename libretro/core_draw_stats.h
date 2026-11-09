/* Test-build event accounting. No clock, formatting, allocation or I/O.
 * Reset only with all instrumented producers quiescent. Snapshots are per-word
 * atomic, not a transactional view of concurrently running producers. */
#ifndef XE_CORE_DRAW_STATS_H
#define XE_CORE_DRAW_STATS_H
#include <stddef.h>
#ifndef XE_GPU_DRAW_STATS
#define XE_GPU_DRAW_STATS 0
#endif
enum xe_draw_role {
   XE_DRAW_UNKNOWN, XE_DRAW_INITIALIZATION, XE_DRAW_COMPILER,
   XE_DRAW_GPU, XE_DRAW_FRONTEND, XE_DRAW_GUEST, XE_DRAW_ROLE_COUNT
};
enum xe_draw_kind {
   XE_DRAW_GUEST_PIPELINE, XE_DRAW_UTILITY_PIPELINE, XE_DRAW_PLACEHOLDER,
   XE_DRAW_KIND_COUNT
};
enum xe_draw_event {
   XE_DRAW_TRANSLATE, XE_DRAW_MODULE, XE_DRAW_GRAPHICS_PSO,
   XE_DRAW_COMPUTE_PSO, XE_DRAW_LINK, XE_DRAW_COMPILE_WAIT,
   XE_DRAW_ADMISSION_WAIT, XE_DRAW_INLINE_JOB, XE_DRAW_INCORRECT_STAND_IN,
   XE_DRAW_MISSING_DRAW, XE_DRAW_EVENT_COUNT
};

#define XE_DRAW_STATS_ABI 1
typedef struct xe_draw_stats {
   unsigned abi_version, role_count, kind_count, event_count;
   size_t events[XE_DRAW_ROLE_COUNT][XE_DRAW_KIND_COUNT][XE_DRAW_EVENT_COUNT];
} xe_draw_stats;
#if XE_GPU_DRAW_STATS
#if defined(XE_DRAW_STATS_IMPLEMENTATION) && defined(_WIN32)
#define XE_DRAW_STATS_API __declspec(dllexport)
#elif defined(XE_DRAW_STATS_IMPLEMENTATION)
#define XE_DRAW_STATS_API __attribute__((visibility("default")))
#else
#define XE_DRAW_STATS_API
#endif
#ifdef __cplusplus
extern "C" {
#endif
XE_DRAW_STATS_API unsigned xe_draw_stats_enter(unsigned role);
XE_DRAW_STATS_API void xe_draw_stats_leave(unsigned previous);
XE_DRAW_STATS_API unsigned xe_draw_stats_role(void);
XE_DRAW_STATS_API void xe_draw_stats_event(unsigned kind, unsigned event);
XE_DRAW_STATS_API void xe_draw_stats_reset(void);
XE_DRAW_STATS_API int xe_draw_stats_snapshot(xe_draw_stats *out, size_t bytes);
XE_DRAW_STATS_API size_t xe_draw_stats_violations(const xe_draw_stats *snapshot);
#ifdef __cplusplus
}
/* Typed lifetime glue only; accounting and policy are C89. */
class xe_draw_role_scope {
 public:
  explicit xe_draw_role_scope(unsigned role) : previous_(xe_draw_stats_enter(role)) {}
  ~xe_draw_role_scope() { xe_draw_stats_leave(previous_); }
 private:
  unsigned previous_;
  xe_draw_role_scope(const xe_draw_role_scope&);
  xe_draw_role_scope& operator=(const xe_draw_role_scope&);
};
#define XE_DRAW_SCOPE(role) xe_draw_role_scope xe_draw_role_scope_instance(role)
#endif
#define XE_DRAW_EVENT(kind,event) xe_draw_stats_event((kind),(event))
#define XE_DRAW_CALL(kind,event,call) (xe_draw_stats_event((kind),(event)),(call))
#else
#define XE_DRAW_SCOPE(role) ((void)0)
#define XE_DRAW_EVENT(kind,event) ((void)0)
#define XE_DRAW_CALL(kind,event,call) (call)
#endif
#endif
