/* Include the real frontend TU to exercise its private lifecycle/runloop,
 * without exporting test hooks or constructing an emulator/backend. */
#define XENIA_LIBRETRO_VERSION "offline fixture"
#define XENIA_LIBRETRO_COMMIT "offline fixture"
#include "../../libretro/libretro.cpp"
extern "C" {
bool xe_failed_env(unsigned,void*);
void xe_failed_poll(void);
int16_t xe_failed_input(unsigned,unsigned,unsigned,unsigned);
void xe_failed_video(const void*,unsigned,unsigned,size_t);
size_t xe_failed_audio(const int16_t*,size_t);
int xe_test_failed_launch_runloop(void) {
  core_state.environ_cb=xe_failed_env; /* Avoid device probing in set_environment. */
  retro_set_input_poll(xe_failed_poll);retro_set_input_state(xe_failed_input);
  retro_set_video_refresh(xe_failed_video);retro_set_audio_sample_batch(xe_failed_audio);
  retro_init();
  game_loaded=false;
  // A poison value models the stale HID view after failed launch destruction.
  // No reference is formed and the guard must never dereference it.
  lr_input_driver=reinterpret_cast<xe::hid::libretro_hid::LibretroInputDriver*>(uintptr_t(0x1001));
  lr_graphics=reinterpret_cast<xe::gpu::GraphicsSystem*>(uintptr_t(0x2001));
  for(unsigned i=0;i<4;++i)retro_run();
  xenia_stop_emulator();
  if(lr_input_driver || lr_graphics || game_loaded)return 0;
  for(unsigned i=0;i<4;++i)retro_run();
  retro_deinit();
  return 1;
}
}
