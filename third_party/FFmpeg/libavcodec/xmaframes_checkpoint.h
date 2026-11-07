/* Owned raw-XMA decoder history checkpoint. MSVC C89 interface.
 * Quiesce the decoder and drain send/receive first. Manual skip samples and
 * default discard policy are required. State metadata contains no pointers.
 * The view is ephemeral: store metadata and history bytes, never the view.
 * Views borrow codec storage until decode, restore, flush, reconfigure or destruction.
 * This is codec history only: caller PCM, packets, cursors and timing are separate. */
#ifndef AVCODEC_XMAFRAMES_CHECKPOINT_H
#define AVCODEC_XMAFRAMES_CHECKPOINT_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
struct AVCodecContext;
#define FF_XMA_CHECKPOINT_VERSION 1
#define FF_XMA_CHECKPOINT_SAMPLES 512
typedef struct FFXmaCheckpointState {
 uint32_t version,rate,channels,samples,frame_number,bit_exact;
 uint32_t trim_start,trim_end,skip_frame,packet_loss;
 uint32_t previous_block[2];
} FFXmaCheckpointState;
typedef struct FFXmaCheckpointView {
 FFXmaCheckpointState state;
 const void *history[2];
 size_t history_bytes[2];
} FFXmaCheckpointView;
int ff_xmaframes_checkpoint_view(const struct AVCodecContext *,FFXmaCheckpointView *);
/* Matching initialized codec only; all config, spans and block lengths are
 * checked before mutation. Source storage must stay immutable and must not
 * alias destination codec storage. No flush, allocation or workspace rebuild. */
int ff_xmaframes_checkpoint_apply(struct AVCodecContext *,const FFXmaCheckpointView *);
#ifdef __cplusplus
}
#endif
#endif
