/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/vfs/devices/xcontent_devices/stfs_container_file.h"
#include "xenia/vfs/devices/xcontent_devices/stfs_container_entry.h"
#include "libretro/core_file_span.h"

namespace xe {
namespace vfs {

StfsContainerFile::StfsContainerFile(uint32_t file_access,
                                     StfsContainerEntry* entry)
    : XContentContainerFile(file_access, entry), entry_(entry) {}

StfsContainerFile::~StfsContainerFile() = default;

void StfsContainerFile::Destroy() { delete this; }

bool StfsContainerFile::CanRead(size_t offset, size_t length, size_t record_file) const {
  return entry_->data() && (!length || entry_->data()->data()) &&
      xe_file_span_range(entry_->data()->size(), 0, offset, length, nullptr);
}

size_t StfsContainerFile::Read(std::span<uint8_t> buffer, size_t offset,
                               size_t record_file) {
  if (!entry_->data() ||
      !xe_file_span_read(entry_->data()->data(), entry_->data()->size(), 0,
                         offset, buffer.data(), buffer.size())) return 0;
  return buffer.size();
}

}  // namespace vfs
}  // namespace xe
