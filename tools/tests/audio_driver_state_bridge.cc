/* Native AudioDriver ABI only; no system, guest CPU, device or frontend. */
#include "libretro/libretro_audio_driver.h"
#include <new>
using xe::apu::libretro::LibretroAudioDriver;
using xe::apu::libretro::LibretroAudioSystem;
extern "C" void *xe_driver_state_new(unsigned rate,unsigned channels,unsigned output,unsigned planar) {
  LibretroAudioSystem::set_output_channels(output);
  LibretroAudioDriver *driver=new(std::nothrow) LibretroAudioDriver(nullptr,nullptr,rate,channels,planar!=0);
  if(driver && !driver->Initialize()) {delete driver;return nullptr;}
  return driver;
}
extern "C" void xe_driver_state_free(void *p) {delete static_cast<LibretroAudioDriver*>(p);}
extern "C" void xe_driver_state_submit(void *p,float *samples) {static_cast<LibretroAudioDriver*>(p)->SubmitFrame(samples);}
extern "C" int xe_driver_state_capture(void *p,xe_audio_driver_view *v) {return static_cast<LibretroAudioDriver*>(p)->CaptureState(v);}
extern "C" int xe_driver_state_ready(void *p,const xe_audio_driver_view *v) {return static_cast<LibretroAudioDriver*>(p)->CanRestoreState(v);}
extern "C" int xe_driver_state_restore(void *p,const xe_audio_driver_view *v) {return static_cast<LibretroAudioDriver*>(p)->RestoreState(v);}
extern "C" void xe_driver_state_stop(void *p) {static_cast<LibretroAudioDriver*>(p)->Shutdown();}
