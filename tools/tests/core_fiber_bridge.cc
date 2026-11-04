#include "xenia/base/threading.h"
extern "C" void* xe_test_fiber_adopt() {
  return xe::threading::Fiber::CreateFromThread().release();
}
extern "C" void* xe_test_fiber_create(void (*entry)(void)) {
  xe::threading::Fiber::CreationParameters params;
  params.stack_size=1024*1024;
  return xe::threading::Fiber::Create(params,entry).release();
}
extern "C" void xe_test_fiber_switch(void* value) {
  static_cast<xe::threading::Fiber*>(value)->SwitchTo();
}
extern "C" void xe_test_fiber_restart(void* value) {
  static_cast<xe::threading::Fiber*>(value)->Restart();
}
extern "C" void xe_test_fiber_delete(void* value) {
  delete static_cast<xe::threading::Fiber*>(value);
}
extern "C" void* xe_test_fiber_current() {
  return xe::threading::Fiber::GetCurrentFiber();
}
extern "C" void xe_test_fiber_shutdown() {
  xe::threading::Fiber::ShutdownStackHandling();
}
