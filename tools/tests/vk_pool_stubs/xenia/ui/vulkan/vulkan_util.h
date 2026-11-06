#ifndef XE_TEST_POOL_UTIL_H
#define XE_TEST_POOL_UTIL_H
namespace xe { namespace ui { namespace vulkan { namespace util {
template <typename Function, typename Device, typename Handle>
void DestroyAndNullHandle(Function destroy, Device device, Handle& handle) {
  if (handle) destroy(device, handle, nullptr);
  handle = Handle{};
}
}}}}
#endif
