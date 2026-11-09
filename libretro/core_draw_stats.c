#define XE_DRAW_STATS_IMPLEMENTATION
#include "core_draw_stats.h"
#if XE_GPU_DRAW_STATS
#include <retro_atomic.h>
#if defined(_MSC_VER)
#define XE_DRAW_TLS __declspec(thread)
#else
#define XE_DRAW_TLS __thread
#endif
static XE_DRAW_TLS unsigned current_role;
static retro_atomic_size_t counts[XE_DRAW_ROLE_COUNT][XE_DRAW_KIND_COUNT][XE_DRAW_EVENT_COUNT];
unsigned xe_draw_stats_enter(unsigned role)
{
   unsigned previous=current_role;
   current_role=role<XE_DRAW_ROLE_COUNT?role:XE_DRAW_UNKNOWN;
   return previous;
}
void xe_draw_stats_leave(unsigned previous)
{current_role=previous<XE_DRAW_ROLE_COUNT?previous:XE_DRAW_UNKNOWN;}
unsigned xe_draw_stats_role(void) {return current_role;}
void xe_draw_stats_event(unsigned kind,unsigned event)
{
   /* Invalid tags must fail closed, rather than hiding an unclassified site. */
   if(kind>=XE_DRAW_KIND_COUNT)kind=XE_DRAW_GUEST_PIPELINE;
   if(event>=XE_DRAW_EVENT_COUNT)event=XE_DRAW_LINK;
   retro_atomic_fetch_add_size(&counts[current_role][kind][event],1);
}
void xe_draw_stats_reset(void)
{
   unsigned r,k,e;
   for(r=0;r<XE_DRAW_ROLE_COUNT;++r)
      for(k=0;k<XE_DRAW_KIND_COUNT;++k)
         for(e=0;e<XE_DRAW_EVENT_COUNT;++e)
            retro_atomic_store_relaxed_size(&counts[r][k][e],0);
}
int xe_draw_stats_snapshot(xe_draw_stats *out, size_t bytes)
{
   unsigned r,k,e;
   if(!out || bytes!=sizeof(*out))return 0;
   out->abi_version=XE_DRAW_STATS_ABI;
   out->role_count=XE_DRAW_ROLE_COUNT;out->kind_count=XE_DRAW_KIND_COUNT;
   out->event_count=XE_DRAW_EVENT_COUNT;
   for(r=0;r<XE_DRAW_ROLE_COUNT;++r)
      for(k=0;k<XE_DRAW_KIND_COUNT;++k)
         for(e=0;e<XE_DRAW_EVENT_COUNT;++e)
            out->events[r][k][e]=retro_atomic_load_acquire_size(&counts[r][k][e]);
   return 1;
}
size_t xe_draw_stats_violations(const xe_draw_stats *snapshot)
{
   size_t count=0;
   unsigned r,k,e;
   if(!snapshot || snapshot->abi_version!=XE_DRAW_STATS_ABI ||
         snapshot->role_count!=XE_DRAW_ROLE_COUNT ||
         snapshot->kind_count!=XE_DRAW_KIND_COUNT ||
         snapshot->event_count!=XE_DRAW_EVENT_COUNT)return (size_t)-1;
   for(r=0;r<XE_DRAW_ROLE_COUNT;++r) {
      if(r==XE_DRAW_INITIALIZATION || r==XE_DRAW_COMPILER)continue;
      for(k=0;k<XE_DRAW_KIND_COUNT;++k)
         for(e=0;e<XE_DRAW_EVENT_COUNT;++e)count+=snapshot->events[r][k][e];
   }
   return count;
}
#endif
