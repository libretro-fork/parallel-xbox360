/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/vfs/devices/xcontent_container_file.h"
#include "xenia/vfs/devices/xcontent_container_entry.h"
#include "libretro/core_file_blocks.h"

namespace xe {
namespace vfs {

XContentContainerFile::XContentContainerFile(uint32_t file_access,
                                             XContentContainerEntry* entry)
    : File(file_access, entry), entry_(entry) {}

XContentContainerFile::~XContentContainerFile() = default;

void XContentContainerFile::Destroy() { delete this; }

X_STATUS XContentContainerFile::ReadSync(std::span<uint8_t> buffer,
                                         size_t byte_offset,
                                         size_t* out_bytes_read) {
  int ok = xe_file_blocks_read(entry_->block_data(), entry_->block_count(),
      entry_->size(), byte_offset, buffer.data(), buffer.size(),
      [](void* context, size_t file, size_t offset, size_t length) -> int {
        return static_cast<XContentContainerFile*>(context)->CanRead(offset, length, file);
      },
      [](void* context, size_t file, size_t offset, void* destination, size_t length) -> size_t {
        return static_cast<XContentContainerFile*>(context)->Read(
            std::span<uint8_t>(static_cast<uint8_t*>(destination), length), offset, file);
      }, this, out_bytes_read);
  return ok ? X_STATUS_SUCCESS : X_STATUS_END_OF_FILE;
}

}  // namespace vfs
}  // namespace xe
