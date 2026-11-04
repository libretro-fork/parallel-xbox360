/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_BYTE_STREAM_H_
#define XENIA_BASE_BYTE_STREAM_H_

#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include "libretro/core_state_io.h"

namespace xe {

// Truncation must fail in release builds too, before a copy or allocation.
struct ByteStreamError {};

class ByteStream {
 public:
  ByteStream(uint8_t* data, size_t data_length, size_t offset = 0,
             bool measuring = false);
  ~ByteStream();

  void Advance(size_t num_bytes);
  void Read(uint8_t* buf, size_t len);
  void Write(const uint8_t* buf, size_t len);

  void Read(void* buf, size_t len) {
    return Read(reinterpret_cast<uint8_t*>(buf), len);
  }
  void Write(const void* buf, size_t len) {
    return Write(reinterpret_cast<const uint8_t*>(buf), len);
  }

  const uint8_t* data() const { return cursor_.data; }
  uint8_t* data() { return cursor_.data; }
  size_t data_length() const { return cursor_.size; }
  bool measuring() const { return cursor_.measuring != 0; }
  const void* ReadView(size_t count) {
    const void* view;
    if (!xe_state_view(&cursor_, &view, count)) throw ByteStreamError{};
    return view;
  }
  std::string_view ReadStringView() {
    xe_state_text text;
    if (!xe_state_read_text(&cursor_, &text)) throw ByteStreamError{};
    return {text.data, text.size};
  }
  size_t remaining() const { return cursor_.size - cursor_.offset; }
  void Require(size_t count) const {
    if (!xe_state_range(&cursor_, count)) throw ByteStreamError{};
  }
  void Patch(size_t offset, const void* data, size_t count) {
    if (!xe_state_patch(&cursor_, offset, data, count)) throw ByteStreamError{};
  }

  size_t offset() const { return cursor_.offset; }
  void set_offset(size_t offset) {
    if (!xe_state_seek(&cursor_, offset)) throw ByteStreamError{};
  }

  template <typename T>
  T Read() {
    static_assert(std::is_trivially_copyable_v<T>, "State values require explicit serialization");
    T data;
    Read(reinterpret_cast<uint8_t*>(&data), sizeof(T));

    return data;
  }

  template <typename T>
  void Write(const T& data) {
    static_assert(std::is_trivially_copyable_v<T>, "State values require explicit serialization");
    Write(reinterpret_cast<const uint8_t*>(&data), sizeof(T));
  }

  void Write(const std::string& str) { Write(std::string_view(str)); }
  void Write(const std::u16string& str) { Write(std::u16string_view(str)); }

  void Write(const std::string_view str) {
    if (!xe_state_write_text(&cursor_, str.data(), str.length())) throw ByteStreamError{};
  }

  void Write(const std::u16string_view str) {
    if (str.length() > UINT32_MAX || str.length() > SIZE_MAX / sizeof(char16_t))
      throw ByteStreamError{};
    Write(uint32_t(str.length()));
    Write(str.data(), str.length() * sizeof(char16_t));
  }

 private:
  xe_state_cursor cursor_;
};

template <>
std::string ByteStream::Read();

template <>
std::u16string ByteStream::Read();
template <>
bool ByteStream::Read();

}  // namespace xe

#endif  // XENIA_BASE_BYTE_STREAM_H_
