/* Legacy FFmpeg header/object ABI only; all test algorithms live in C89. */
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/log.h>
}
#include <cstdlib>
struct XmaFixture { AVCodecContext *context; AVPacket *packet; AVFrame *frame; };
extern "C" void xe_xma_test_free(void *p) {
 XmaFixture *f=(XmaFixture*)p;if(!f) return;
 av_frame_free(&f->frame);av_packet_free(&f->packet);avcodec_free_context(&f->context);free(f);
}
extern "C" void *xe_xma_test_new(int rate,unsigned channels) {
 const AVCodec *codec=avcodec_find_decoder(AV_CODEC_ID_XMAFRAMES);
 XmaFixture *f=(XmaFixture*)calloc(1,sizeof(*f));
 if(!f) return nullptr;
 av_log_set_level(AV_LOG_QUIET);
 f->context=avcodec_alloc_context3(codec);f->packet=av_packet_alloc();f->frame=av_frame_alloc();
 if(!f->context || !f->packet || !f->frame || av_new_packet(f->packet,16)<0) goto fail;
 f->context->sample_rate=rate;av_channel_layout_default(&f->context->ch_layout,(int)channels);
 f->context->flags2|=AV_CODEC_FLAG2_SKIP_MANUAL;
 if(avcodec_open2(f->context,codec,nullptr)<0) goto fail;
 return f;
fail: xe_xma_test_free(f);return nullptr;
}
extern "C" AVCodecContext *xe_xma_test_context(void *p) {return p?((XmaFixture*)p)->context:nullptr;}
extern "C" unsigned char *xe_xma_test_packet(void *p) {return ((XmaFixture*)p)->packet->data;}
extern "C" int xe_xma_test_send(void *p,unsigned bytes) {
 XmaFixture *f=(XmaFixture*)p;f->packet->size=(int)bytes;return avcodec_send_packet(f->context,f->packet);
}
extern "C" int xe_xma_test_receive(void *p) {
 XmaFixture *f=(XmaFixture*)p;int result=avcodec_receive_frame(f->context,f->frame);
 return result<0?result:f->frame->nb_samples;
}
extern "C" const void *xe_xma_test_samples(void *p,unsigned channel) {return ((XmaFixture*)p)->frame->extended_data[channel];}
