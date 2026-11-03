/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_UTIL_XLAST_H_
#define XENIA_KERNEL_UTIL_XLAST_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "libretro/core_xlast.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {
namespace util {

class XLastMatchmakingQuery {
 public:
  XLastMatchmakingQuery();
  explicit XLastMatchmakingQuery(const xe_xlast_query* query_node);

  std::string GetName() const;
  std::vector<uint32_t> GetReturns() const;
  std::vector<uint32_t> GetParameters() const;
  std::vector<uint32_t> GetFilters() const;

 private:
  const xe_xlast_query* node_ = nullptr;
};

class XLast {
 public:
  XLast() = default;
  XLast(const uint8_t* compressed_xml_data, const uint32_t compressed_data_size,
        const uint32_t decompressed_data_size);
  ~XLast() = default;

  std::u16string GetTitleName() const;
  unsigned GetProductInformationAttributes(uint32_t values[7]) const;

  const char* GetLocalizedStringUtf8(uint32_t string_id, XLanguage language) const;
  std::vector<XLanguage> GetSupportedLanguages() const;
  std::u16string GetLocalizedString(uint32_t string_id,
                                    XLanguage language) const;
  const std::optional<uint32_t> GetPresenceStringId(const uint32_t context_id);
  const std::optional<uint32_t> GetPropertyStringId(const uint32_t property_id);
  const std::u16string GetPresenceRawString(const uint32_t presence_value,
                                            const XLanguage language);
  const std::optional<uint32_t> GetContextStringId(
      const uint32_t context_id, const uint32_t context_value);
  std::optional<XLastMatchmakingQuery> GetMatchmakingQuery(uint32_t query_id) const;
  bool HasXLast() const { return parsed_xlast_ != nullptr; }

 private:
  struct Delete {
    void operator()(xe_xlast* doc) const { xe_xlast_destroy(doc); }
  };
  std::unique_ptr<xe_xlast, Delete> parsed_xlast_;
};

}  // namespace util
}  // namespace kernel
}  // namespace xe

#endif
