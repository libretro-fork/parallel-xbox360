/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/embedded_bundle.h"

#include <cstring>

#include "libretro/core_decompress.h"
#include "xenia/base/logging.h"

namespace xe {

EmbeddedBundle::EmbeddedBundle(const unsigned char* compressed_data,
                               size_t compressed_size, size_t raw_size) {
  uint8_t* bytes = nullptr;
  size_t decoded_size = 0;
  if (!raw_size || !xe_decompress_alloc(&bytes, &decoded_size,
          compressed_data, compressed_size, 15, raw_size, raw_size)) {
    XELOGE("EmbeddedBundle: complete-stream decompression failed");
    return;
  }
  if (decoded_size != raw_size) {
    std::free(bytes);
    XELOGE("EmbeddedBundle: decompressed size mismatch");
    return;
  }
  raw_.reset(bytes);
  raw_size_ = decoded_size;
}

void EmbeddedBundle::ForEach(const VisitFn& visit) const {
  if (raw_size_ < sizeof(uint32_t)) {
    return;
  }
  const uint8_t* p = raw_.get();
  const uint8_t* end = p + raw_size_;

  uint32_t count;
  std::memcpy(&count, p, sizeof(count));
  p += sizeof(count);

  for (uint32_t i = 0; i < count; ++i) {
    if (size_t(end - p) < sizeof(uint32_t)) {
      return;
    }
    uint32_t name_len;
    std::memcpy(&name_len, p, sizeof(name_len));
    p += sizeof(name_len);
    if (size_t(end - p) < name_len) {
      return;
    }
    std::string_view name(reinterpret_cast<const char*>(p), name_len);
    p += name_len;

    if (size_t(end - p) < sizeof(uint32_t)) {
      return;
    }
    uint32_t data_len;
    std::memcpy(&data_len, p, sizeof(data_len));
    p += sizeof(data_len);
    if (size_t(end - p) < data_len) {
      return;
    }
    std::string_view data(reinterpret_cast<const char*>(p), data_len);
    p += data_len;

    visit(name, data);
  }
}

}  // namespace xe
