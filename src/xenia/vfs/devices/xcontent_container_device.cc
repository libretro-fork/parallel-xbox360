/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/vfs/devices/xcontent_container_device.h"
#include "xenia/base/logging.h"
#include "xenia/base/string.h"
#include "libretro/core_package_header.h"
#include <cstddef>
#include "xenia/vfs/devices/xcontent_devices/stfs_container_device.h"
#include "xenia/vfs/devices/xcontent_devices/svod_container_device.h"

namespace xe {
namespace vfs {

std::unique_ptr<XContentContainerDevice>
XContentContainerDevice::CreateContentDevice(
    const std::string_view mount_path, const std::filesystem::path& host_path) {
  static_assert(sizeof(decltype(XContentMetadata::volume_type)) == 4);
  const auto path = xe::path_to_utf8(host_path);
  unsigned volume;
  if (xe_package_probe_type(path.c_str(), sizeof(XContentContainerHeader),
          offsetof(XContentContainerHeader, content_metadata) +
          offsetof(XContentMetadata, volume_type), &volume) != 1) return nullptr;

  switch (static_cast<XContentVolumeType>(volume)) {
    case XContentVolumeType::kStfs:
      return std::make_unique<StfsContainerDevice>(mount_path, host_path);
      break;
    case XContentVolumeType::kSvod:
      return std::make_unique<SvodContainerDevice>(mount_path, host_path);
      break;
    default:
      break;
  }

  return nullptr;
}

XContentContainerDevice::XContentContainerDevice(
    const std::string_view mount_path, const std::filesystem::path& host_path)
    : Device(mount_path),
      name_("XContent"),
      host_path_(host_path),
      files_total_size_(0),
      header_(std::make_unique<XContentContainerHeader>()) {}

XContentContainerDevice::~XContentContainerDevice() {}

bool XContentContainerDevice::Initialize() {
  XELOGI("Loading XContent header file: {}", host_path_);
  auto header_loading_result = ReadHeaderAndVerify();
  if (header_loading_result != Result::kSuccess) {
    XELOGE("Error reading XContent header: {}",
           static_cast<int32_t>(header_loading_result));
    return false;
  }

  SetupContainer();

  if (LoadHostFiles() != Result::kSuccess) {
    XELOGE("Error loading XContent host files.");
    return false;
  }

  return Read() == Result::kSuccess;
}

std::unique_ptr<XContentContainerHeader>
XContentContainerDevice::ReadContainerHeader(
    const std::filesystem::path& file_path) {
  const auto path = xe::path_to_utf8(file_path);
  std::unique_ptr<XContentContainerHeader> header(new XContentContainerHeader);
  if (xe_package_read_header(path.c_str(), header.get(), sizeof(*header), nullptr) != 1)
    return nullptr;
  return header;
}

Entry* XContentContainerDevice::ResolvePath(const std::string_view path) {
  // The filesystem will have stripped our prefix off already, so the path will
  // be in the form:
  // some\PATH.foo
  XELOGFS("StfsContainerDevice::ResolvePath({})", path);
  return root_entry_->ResolvePath(path);
}

void XContentContainerDevice::Dump(StringBuffer* string_buffer) {
  auto global_lock = global_critical_region_.Acquire();
  root_entry_->Dump(string_buffer, 0);
}

kernel::xam::XCONTENT_AGGREGATE_DATA
XContentContainerDevice::ContentDataFromHeader(
    const XContentContainerHeader& header) {
  kernel::xam::XCONTENT_AGGREGATE_DATA data;

  std::memset(&data, 0, sizeof(kernel::xam::XCONTENT_AGGREGATE_DATA));

  data.device_id = 1;
  data.title_id = header.content_metadata.execution_info.title_id;
  data.content_type = header.content_metadata.content_type;

  auto name = header.content_metadata.display_name(XLanguage::kEnglish);
  if (name.empty()) {
    // Find first filled language and use it. It might be incorrect, but meh
    // until stfs support is done.
    for (uint8_t i = 0; i < header.content_metadata.kNumLanguagesV2; i++) {
      name = header.content_metadata.display_name((XLanguage)i);
      if (!name.empty()) {
        break;
      }
    }
  }

  data.set_display_name(name);

  return data;
}

kernel::xam::XCONTENT_AGGREGATE_DATA XContentContainerDevice::content_header()
    const {
  return ContentDataFromHeader(*header_);
}

XContentContainerDevice::Result XContentContainerDevice::ReadHeaderAndVerify() {
  const auto path = xe::path_to_utf8(host_path_);
  uint64_t size;
  int result = xe_package_read_header(path.c_str(), header_.get(), sizeof(*header_), &size);
  if (!result) return Result::kTooSmall;
  if (result < 0) return Result::kReadError;
  if (!header_->content_header.is_magic_valid()) return Result::kFileMismatch;
  files_total_size_ = size;
  return Result::kSuccess;
}

}  // namespace vfs
}  // namespace xe
