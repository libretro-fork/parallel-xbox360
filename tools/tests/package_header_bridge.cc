/* Actual container path/object ABI; all I/O arrives at the C89 hostile VFS. */
#include "xenia/vfs/devices/xcontent_container_device.h"
#include "xenia/vfs/devices/xcontent_devices/stfs_container_device.h"
#include "xenia/vfs/devices/xcontent_devices/svod_container_device.h"
#include "xenia/base/string.h"
#include <cstddef>
#include <cstring>
using xe::vfs::XContentContainerDevice;
using xe::vfs::XContentContainerHeader;
class HeaderDevice : public XContentContainerDevice {
 public:
  explicit HeaderDevice(const char* path)
      : XContentContainerDevice("package:", xe::to_path(std::string_view(path))) {}
  uint32_t component_name_max_length() const override { return 255; }
  uint32_t total_allocation_units() const override { return 0; }
  uint32_t available_allocation_units() const override { return 0; }
  unsigned payload_calls() const { return payload_calls_; }
 protected:
  Result LoadHostFiles() override { ++payload_calls_; return Result::kSuccess; }
  Result Read() override { ++payload_calls_; return Result::kSuccess; }
 private:
  unsigned payload_calls_ = 0;
};
extern "C" size_t xe_package_native_size() { return sizeof(XContentContainerHeader); }
extern "C" size_t xe_package_native_volume_offset() {
  return offsetof(XContentContainerHeader, content_metadata) +
      offsetof(xe::vfs::XContentMetadata, volume_type);
}
extern "C" int xe_package_native_type(const char* path) {
  try {
    auto device = XContentContainerDevice::CreateContentDevice("package:", xe::to_path(std::string_view(path)));
    if (dynamic_cast<xe::vfs::StfsContainerDevice*>(device.get())) return 0;
    if (dynamic_cast<xe::vfs::SvodContainerDevice*>(device.get())) return 1;
  } catch (...) {}
  return -1;
}
extern "C" int xe_package_native_read(const char* path, const void* expected, size_t size) {
  try {
    auto header = XContentContainerDevice::ReadContainerHeader(xe::to_path(std::string_view(path)));
    return header && size == sizeof(*header) && !std::memcmp(header.get(), expected, size);
  } catch (...) { return 0; }
}
extern "C" int xe_package_native_initialize(const char* path, const void* expected, size_t size) {
  try {
    HeaderDevice device(path);
    const auto* destination = device.GetContainerHeader();
    if (!device.Initialize()) return device.payload_calls() ? -1 : 0;
    return device.payload_calls() == 2 && device.GetContainerHeader() == destination &&
        size == sizeof(*destination) && !std::memcmp(destination, expected, size);
  } catch (...) { return 0; }
}
