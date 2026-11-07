/* Minimal ABI bridge: all trace algorithms and COM mocks live in C89. */
#include <new>
#include "xenia/gpu/d3d12/deferred_command_list.h"
using Commands = xe::gpu::d3d12::DeferredCommandList;
extern "C" void* xe_test_barriers_new() { return new Commands(4096); }
extern "C" void xe_test_barriers_delete(void* p) { delete static_cast<Commands*>(p); }
extern "C" void* xe_test_barriers_append(void* p) { return &static_cast<Commands*>(p)->AppendResourceBarrier(); }
extern "C" void xe_test_barriers_pop(void* p) { static_cast<Commands*>(p)->CancelResourceBarrier(); }
extern "C" void xe_test_barriers_submit(void* p) { static_cast<Commands*>(p)->SubmitResourceBarriers(); }
extern "C" void xe_test_barriers_marker(void* p, unsigned value) { static_cast<Commands*>(p)->D3DOMSetStencilRef(value); }
extern "C" void xe_test_barriers_copy(void* p, const void* row) {
  static_cast<Commands*>(p)->D3DResourceBarrier(1, static_cast<const D3D12_RESOURCE_BARRIER*>(row));
}
extern "C" void xe_test_barriers_execute(void* p, void* mock) {
  static_cast<Commands*>(p)->Execute(static_cast<ID3D12GraphicsCommandList*>(mock), nullptr, nullptr);
}
extern "C" void xe_test_barriers_reset(void* p) { static_cast<Commands*>(p)->Reset(); }
extern "C" void* xe_test_barriers_sized(size_t size) { return new Commands(size); }
extern "C" int xe_test_barriers_try_new() {
  try { Commands commands(24); return 1; } catch (const std::bad_alloc&) { return 0; }
}
extern "C" int xe_test_barriers_try_submit(void* p) {
  try { static_cast<Commands*>(p)->SubmitResourceBarriers(); return 1; }
  catch (const std::bad_alloc&) { return 0; }
}