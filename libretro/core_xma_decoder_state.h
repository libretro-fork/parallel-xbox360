/* Borrowed whole raw-XMA decoder capture. MSVC C89 metadata validation. */
#ifndef XE_CORE_XMA_DECODER_STATE_H
#define XE_CORE_XMA_DECODER_STATE_H
#include "libretro/core_xma_owner_state.h"
enum { XE_XMA_CONTEXTS=320, XE_XMA_REGISTERS=16384,
       XE_XMA_MODE_FAKE=0, XE_XMA_MODE_MASTER=1, XE_XMA_MODE_OLD=2,
       XE_XMA_MODE_RAW=3, XE_XMA_MODE_UNSET=4 };
typedef struct xe_xma_decoder_state {
 uint32_t version, mode, count, context_first, context_last;
} xe_xma_decoder_state;
/* Caller-owned reusable storage, not a worker-stack temporary or wire record.
 * Registers/history/PCM borrow their owners until mutation/destruction. Failed
 * capture clears version; partially filled output must never be consumed. */
typedef struct xe_xma_decoder_view {
 xe_xma_decoder_state state;
 const uint32_t *registers;
 size_t register_bytes;
 uint64_t free_slots[5];
 xe_xma_owner_view owners[XE_XMA_CONTEXTS];
} xe_xma_decoder_view;
static INLINE int xe_xma_decoder_view_valid(const xe_xma_decoder_view *view,
      uint32_t first,uint32_t last)
{
 uint32_t i,offset,bytes,free_bit;
 if (!view || view->state.version!=1 || view->state.mode!=XE_XMA_MODE_RAW ||
       view->state.count!=XE_XMA_CONTEXTS || !first || (first&63) ||
       first>UINT32_MAX-(XE_XMA_CONTEXTS*64-1) ||
       last!=first+XE_XMA_CONTEXTS*64-1 || view->state.context_first!=first ||
       view->state.context_last!=last || !view->registers ||
       view->register_bytes!=XE_XMA_REGISTERS*sizeof(uint32_t) ||
       (uintptr_t)view->registers>UINTPTR_MAX-view->register_bytes) return 0;
 for(i=0;i<XE_XMA_CONTEXTS;++i) {
  if(!xe_xma_owner_plan(&view->owners[i],i,first+i*64,&offset,&bytes)) return 0;
  free_bit=(uint32_t)((view->free_slots[i/64]>>(63-(i&63)))&1);
  if(free_bit==view->owners[i].state.allocated) return 0;
 }
 return 1;
}
#endif
