/* Offline raw-XMA decoder comparison. No frontend, device or guest execution. */
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <formats/rwma.h>
#include "libavcodec/avcodec.h"
#if defined(_WIN32)
#include <windows.h>
#else
#include <time.h>
#endif

typedef struct xma_bench_record {
   unsigned size, offset, bits;
   unsigned char *data;
   AVPacket *packet;
} xma_bench_record;
static xma_bench_record records[1024];
static volatile float checksum;

static double seconds(void)
{
#if defined(_WIN32)
   LARGE_INTEGER ticks, frequency;
   QueryPerformanceCounter(&ticks);
   QueryPerformanceFrequency(&frequency);
   return (double)ticks.QuadPart / (double)frequency.QuadPart;
#else
   struct timespec ticks;
   if (clock_gettime(CLOCK_MONOTONIC, &ticks)) return -1;
   return (double)ticks.tv_sec + (double)ticks.tv_nsec / 1000000000.0;
#endif
}
static unsigned load32(const unsigned char *p)
{
   return p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) |
      ((unsigned)p[3] << 24);
}
static unsigned read_records(FILE *in)
{
   unsigned char header[12];
   unsigned count = 0;
   size_t bytes;
   while ((bytes = fread(header, 1, sizeof(header), in)) != 0) {
      xma_bench_record *r;
      unsigned tail;
      if (bytes != sizeof(header) || count == 1024) return 0;
      r = &records[count++];
      r->size = load32(header); r->offset = load32(header + 4);
      r->bits = load32(header + 8);
      if (!r->size || r->size > 4098 || r->offset > 7 || r->bits < 18 ||
            r->bits >= 32767 || r->offset + r->bits > r->size * 8) return 0;
      r->data = (unsigned char *)malloc(r->size);
      r->packet = av_packet_alloc();
      if (!r->data || !r->packet || fread(r->data, 1, r->size, in) != r->size ||
            av_new_packet(r->packet, (int)r->size + 1) < 0) return 0;
      tail = (8 - ((r->offset + r->bits) & 7)) & 7;
      r->packet->data[0] = (unsigned char)((r->offset << 5) | (tail << 2));
      memcpy(r->packet->data + 1, r->data, r->size);
   }
   return ferror(in) ? 0 : count;
}
static int reference(AVCodecContext *codec, AVFrame *frame, unsigned index)
{
   return avcodec_send_packet(codec, records[index].packet) >= 0 &&
      avcodec_receive_frame(codec, frame) >= 0;
}
int main(int argc, char **argv)
{
   rwma_config cfg;
   rwma_t *native = NULL;
   rwma_frame pcm;
   const AVCodec *decoder = avcodec_find_decoder(AV_CODEC_ID_XMAFRAMES);
   AVCodecContext *codec = avcodec_alloc_context3(decoder);
   AVFrame *frame = av_frame_alloc();
   FILE *input;
   unsigned count, channels, i, round, phase, repeat, repeats, ch, sample;
   int result = 1;
   double max_error = 0;
   if (argc != 3) {
      fprintf(stderr, "usage: xma_native_benchmark records.bin channels\n");
      goto done;
   }
   channels = (unsigned)strtoul(argv[2], NULL, 10);
   if (!decoder || !codec || !frame || (channels != 1 && channels != 2)) goto done;
   input = fopen(argv[1], "rb");
   if (!input) goto done;
   count = read_records(input);
   if (fclose(input) || !count) goto done;
   memset(&cfg, 0, sizeof(cfg));
   cfg.codec = RWMA_XMA_RAW; cfg.sample_rate = 48000; cfg.channels = channels;
   codec->sample_rate = 48000; codec->ch_layout.nb_channels = channels;
   codec->flags |= AV_CODEC_FLAG_BITEXACT;
   codec->flags2 |= AV_CODEC_FLAG2_SKIP_MANUAL;
   if (rwma_create(&native, &cfg) || avcodec_open2(codec, decoder, NULL) < 0) goto done;
   for (i = 0; i < count; i++) {
      xma_bench_record *r = &records[i];
      if (rwma_decode_xma(native, r->data, r->size, r->offset, r->bits, &pcm) ||
            !reference(codec, frame, i) || pcm.channels != channels ||
            pcm.samples != (unsigned)frame->nb_samples) goto done;
      for (ch = 0; ch < channels; ch++) for (sample = 0; sample < pcm.samples; sample++) {
         double expected = ((const float *)frame->extended_data[ch])[sample];
         double actual = pcm.planes[ch][sample], error = fabs(actual - expected);
         if (!(error <= 0.0001 + fabs(expected) * 0.0001)) goto done;
         if (error > max_error) max_error = error;
      }
      av_frame_unref(frame);
   }
   printf("correctness: %u frames, %u channels, max float error %.9g, libavcodec headers %u\n",
         count, channels, max_error, LIBAVCODEC_VERSION_INT);
   repeats = (20000 + count - 1) / count;
   puts("round,decoder,microseconds_per_frame,frames");
   for (round = 0; round < 8; round++) for (phase = 0; phase < 2; phase++) {
      int use_native = ((round & 1) ^ phase) == 0;
      double start = seconds(), elapsed;
      if (start < 0) goto done;
      for (repeat = 0; repeat < repeats; repeat++) {
         if (use_native) rwma_reset(native); else avcodec_flush_buffers(codec);
         for (i = 0; i < count; i++) {
            xma_bench_record *r = &records[i];
            if (use_native) {
               if (rwma_decode_xma(native,r->data,r->size,r->offset,r->bits,&pcm)) goto done;
               checksum += pcm.planes[0][0];
            } else {
               if (!reference(codec,frame,i)) goto done;
               checksum += ((const float *)frame->extended_data[0])[0];
               av_frame_unref(frame);
            }
         }
      }
      elapsed = seconds() - start;
      if (elapsed <= 0) goto done;
      printf("%u,%s,%.6f,%u\n",round,use_native ? "native" : "ffmpeg",
            elapsed * 1000000.0 / (repeats * count),repeats * count);
      fflush(stdout);
   }
   result = 0;
done:
   for (i = 0; i < 1024; i++) {
      free(records[i].data);
      av_packet_free(&records[i].packet);
   }
   rwma_free(native); avcodec_free_context(&codec); av_frame_free(&frame);
   if (result) fputs("FAIL: corpus, decoder, correctness or timing unavailable\n",stderr);
   return result;
}
