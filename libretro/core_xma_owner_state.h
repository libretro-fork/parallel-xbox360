/* Exclusive/quiescent XMA owner checkpoints. MSVC C89 metadata and planning. */
#ifndef XE_CORE_XMA_OWNER_STATE_H
#define XE_CORE_XMA_OWNER_STATE_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <retro_inline.h>
#include "third_party/FFmpeg/libavcodec/xmaframes_checkpoint.h"
typedef struct xe_xma_owner_state {
 uint32_t version,id,guest_ptr,allocated,enabled;
 uint32_t rate,channels,pending_blocks,loop_limit,loop_start_pending;
} xe_xma_owner_state;
typedef struct xe_xma_owner_view {
 xe_xma_owner_state state;
 FFXmaCheckpointView codec;
 const void *pcm;
 size_t pcm_bytes;
} xe_xma_owner_view;
/* Runtime resource owner, never serialized. Zero initialize; do not copy it.
 * Prepare borrows only PCM until commit/discard; codec history is copied directly
 * into the candidate codec that commit adopts. Owner and all callers stay paused. */
typedef struct xe_xma_owner_stage {
 xe_xma_owner_state state;
 const void *pcm;
 size_t pcm_bytes;
 struct AVCodecContext *candidate;
 const void *owner;
 const struct AVCodecContext *previous;
} xe_xma_owner_stage;
static INLINE int xe_xma_owner_layout(const xe_xma_owner_state *state,
      uint32_t *offset,uint32_t *bytes)
{
 uint32_t blocks;
 if (!state || !offset || !bytes || state->version!=1 || state->id>=320 ||
       state->allocated>1 || state->enabled>1 || state->loop_start_pending>1 ||
       state->loop_limit>16) return 0;
 if (!state->rate) {
  if (state->channels || state->pending_blocks || state->loop_limit) return 0;
  *offset=0;*bytes=0;return 1;
 }
 if ((state->rate!=24000 && state->rate!=32000 && state->rate!=44100 &&
       state->rate!=48000) || state->channels<1 || state->channels>2) return 0;
 blocks=state->channels*4;
 if (state->pending_blocks>blocks) return 0;
 *offset=state->pending_blocks ? (blocks-state->pending_blocks)*256 : 0;
 *bytes=state->pending_blocks*256;
 return 1;
}
static INLINE int xe_xma_owner_plan(const xe_xma_owner_view *view,
      uint32_t id,uint32_t guest_ptr,uint32_t *offset,uint32_t *bytes)
{
 FFXmaCheckpointState empty;
 if (!view || !xe_xma_owner_layout(&view->state,offset,bytes) ||
       view->state.id!=id || view->state.guest_ptr!=guest_ptr ||
       view->pcm_bytes!=*bytes || (!!view->pcm!=!!*bytes)) return 0;
 if (*bytes && (uintptr_t)view->pcm>UINTPTR_MAX-*bytes) return 0;
 if (view->state.rate) return view->codec.state.rate==view->state.rate &&
       view->codec.state.channels==view->state.channels;
 memset(&empty,0,sizeof(empty));
 return !memcmp(&view->codec.state,&empty,sizeof(empty)) &&
       !view->codec.history[0] && !view->codec.history[1] &&
       !view->codec.history_bytes[0] && !view->codec.history_bytes[1];
}
#endif
