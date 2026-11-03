/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/util/xlast.h"
#include "xenia/base/logging.h"
#include "xenia/base/string.h"

namespace xe {
namespace kernel {
namespace util {
namespace {
void AppendNumber(void* out, uint32_t value) {
  static_cast<std::vector<uint32_t>*>(out)->push_back(value);
}
void AppendLanguage(void* out, uint32_t value) {
  static_cast<std::vector<XLanguage>*>(out)->push_back(static_cast<XLanguage>(value));
}
std::vector<uint32_t> QueryValues(const xe_xlast_query* query, unsigned kind) {
  std::vector<uint32_t> result;
  result.reserve(xe_xlast_query_values(query, kind, nullptr, nullptr));
  xe_xlast_query_values(query, kind, AppendNumber, &result);
  return result;
}
}  // namespace

XLastMatchmakingQuery::XLastMatchmakingQuery() = default;
XLastMatchmakingQuery::XLastMatchmakingQuery(const xe_xlast_query* query_node)
    : node_(query_node) {}
std::string XLastMatchmakingQuery::GetName() const {
  return xe_xlast_query_name(node_);
}
std::vector<uint32_t> XLastMatchmakingQuery::GetReturns() const {
  return QueryValues(node_, 0);
}
std::vector<uint32_t> XLastMatchmakingQuery::GetParameters() const {
  return QueryValues(node_, 1);
}
std::vector<uint32_t> XLastMatchmakingQuery::GetFilters() const {
  return QueryValues(node_, 2);
}
XLast::XLast(const uint8_t* data, const uint32_t size, const uint32_t decoded) {
  parsed_xlast_.reset(xe_xlast_create_gzip(data, size, decoded));
  if (!parsed_xlast_) XELOGW("XLast: missing, corrupt or invalid XML metadata");
}
std::u16string XLast::GetTitleName() const {
  return xe::to_utf16(xe_xlast_title(parsed_xlast_.get()));
}
unsigned XLast::GetProductInformationAttributes(uint32_t values[7]) const {
  return xe_xlast_product(parsed_xlast_.get(), values);
}
const char* XLast::GetLocalizedStringUtf8(uint32_t id, XLanguage language) const {
  return xe_xlast_localized(parsed_xlast_.get(), id, static_cast<uint32_t>(language));
}
std::vector<XLanguage> XLast::GetSupportedLanguages() const {
  std::vector<XLanguage> result;
  result.reserve(xe_xlast_languages(parsed_xlast_.get(), nullptr, nullptr));
  xe_xlast_languages(parsed_xlast_.get(), AppendLanguage, &result);
  return result;
}
std::u16string XLast::GetLocalizedString(uint32_t id, XLanguage language) const {
  return xe::to_utf16(xe_xlast_localized(parsed_xlast_.get(), id, static_cast<uint32_t>(language)));
}
const std::optional<uint32_t> XLast::GetPresenceStringId(const uint32_t id) {
  uint32_t value;
  if (xe_xlast_presence(parsed_xlast_.get(), id, &value)) return value;
  return std::nullopt;
}
const std::optional<uint32_t> XLast::GetPropertyStringId(const uint32_t id) {
  uint32_t value;
  if (xe_xlast_property(parsed_xlast_.get(), id, &value)) return value;
  return std::nullopt;
}
const std::u16string XLast::GetPresenceRawString(const uint32_t value, const XLanguage language) {
  const auto id = GetPresenceStringId(value);
  return id ? GetLocalizedString(*id, language) : std::u16string();
}
const std::optional<uint32_t> XLast::GetContextStringId(const uint32_t id, const uint32_t context) {
  uint32_t value;
  if (xe_xlast_context(parsed_xlast_.get(), id, context, &value)) return value;
  return std::nullopt;
}
std::optional<XLastMatchmakingQuery> XLast::GetMatchmakingQuery(uint32_t id) const {
  const auto* node = xe_xlast_find_query(parsed_xlast_.get(), id);
  if (node) return XLastMatchmakingQuery(node);
  return std::nullopt;
}
}  // namespace util
}  // namespace kernel
}  // namespace xe
