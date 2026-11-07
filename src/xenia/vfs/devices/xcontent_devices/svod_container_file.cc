/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/vfs/devices/xcontent_devices/svod_container_file.h"
#include "xenia/vfs/devices/xcontent_devices/svod_container_entry.h"

namespace xe {
namespace vfs {

SvodContainerFile::SvodContainerFile(uint32_t file_access,
                                     SvodContainerEntry* entry)
    : XContentContainerFile(file_access, entry), entry_(entry) {}

SvodContainerFile::~SvodContainerFile() = default;

void SvodContainerFile::Destroy() { delete this; }

bool SvodContainerFile::CanRead(size_t offset, size_t length, size_t record_file) const {
  return xe_svod_files_range(entry_->files(),record_file,offset,length) != 0;
}

size_t SvodContainerFile::Read(std::span<uint8_t> buffer, size_t offset,
                               size_t record_file) {
  return xe_svod_files_transfer(entry_->files(),record_file,offset,
                                buffer.data(),buffer.size());
}

}  // namespace vfs
}  // namespace xe
