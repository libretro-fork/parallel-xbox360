/* Offline frontend callbacks; no content, GPU device or JIT is started. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include "libretro.h"
static unsigned polls, states, videos, batches, bad, disc_answers;
static size_t samples;
bool xe_failed_env(unsigned command, void *data) { (void)command; (void)data; return false; }
void xe_failed_poll(void) { ++polls; }
int16_t xe_failed_input(unsigned port,unsigned device,unsigned index,unsigned id) {
  (void)port;(void)device;(void)index;(void)id;++states;return 0;
}
void xe_failed_video(const void *data,unsigned width,unsigned height,size_t pitch) {
  if(data || width!=1280 || height!=720 || pitch!=5120)++bad;
  ++videos;
}
size_t xe_failed_audio(const int16_t *data,size_t frames) {
  size_t i;
  if(frames!=800)++bad;
  for(i=0;i<frames*2;++i)if(data[i])++bad;
  samples+=frames;++batches;return frames;
}
void xe_failed_disc_answer(void *context, const char *path) {
  if(context || strcmp(path,"disc.iso"))++bad;
  ++disc_answers;
}
extern int xe_test_failed_launch_runloop(void);
int main(void) {
  if(!xe_test_failed_launch_runloop() || polls!=8 || states || videos!=8 || batches!=8 || samples!=6400 || disc_answers!=2 || bad)return 1;
  puts("Failed-launch runloop: no stale HID calls, stop clears input/graphics views, eight consecutive video/audio callbacks and two cancelled disc waiters pass without GPU/JIT setup");
  return 0;
}
