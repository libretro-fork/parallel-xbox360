/* Native object ABI only: no Setup, device, frontend, worker or guest JIT. */
#include "xenia/gpu/graphics_system.h"
#include "xenia/gpu/command_processor.h"
class ColdGraphics final : public xe::gpu::GraphicsSystem {
 public:
  std::string name() const override {return "cold-test";}
 protected:
  std::unique_ptr<xe::gpu::CommandProcessor> CreateCommandProcessor() override {return {};}
};
extern "C" void* xe_test_graphics_new() noexcept {
 try {return new ColdGraphics;} catch(...) {return nullptr;}
}
extern "C" void* xe_test_graphics_registers(void* p) noexcept {
 return static_cast<ColdGraphics*>(p)->register_file();
}
extern "C" int xe_test_graphics_pause(void* p) noexcept {
 auto* graphics=static_cast<ColdGraphics*>(p);
 if(graphics->PauseForState(0) || graphics->is_paused()) return 0;
 graphics->Resume();return !graphics->is_paused();
}
extern "C" void xe_test_graphics_free(void* p) noexcept {delete static_cast<ColdGraphics*>(p);}
