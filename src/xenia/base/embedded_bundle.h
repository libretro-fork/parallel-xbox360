/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_EMBEDDED_BUNDLE_H_
#define XENIA_BASE_EMBEDDED_BUNDLE_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string_view>
#include <memory>
#include <cstdlib>

namespace xe {

// Decompresses a zlib-compressed concat blob produced by tools/build/
// core_codegen.c and exposes a callback iteration over (name, data) entries.
// Holds the inflated bytes; emitted string_views point into it, so the
// EmbeddedBundle must outlive any view captured during ForEach.
class EmbeddedBundle {
 public:
  EmbeddedBundle(const unsigned char* compressed_data, size_t compressed_size,
                 size_t raw_size);

  bool ok() const { return raw_ != nullptr; }

  using VisitFn =
      std::function<void(std::string_view name, std::string_view data)>;
  void ForEach(const VisitFn& visit) const;

 private:
  std::unique_ptr<uint8_t, decltype(&std::free)> raw_{nullptr, &std::free};
  size_t raw_size_ = 0;
};

}  // namespace xe

#endif  // XENIA_BASE_EMBEDDED_BUNDLE_H_
