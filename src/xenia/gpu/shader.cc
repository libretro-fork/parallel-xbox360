/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/shader.h"

#include <cstring>
#include <new>

#include "third_party/fmt/include/fmt/format.h"
#include "xenia/base/filesystem.h"
#include "xenia/base/math.h"
#include "xenia/base/memory.h"
#include "xenia/base/string.h"
#include "xenia/gpu/ucode.h"

namespace xe {
namespace gpu {
using namespace ucode;

Shader::Shader(xenos::ShaderType shader_type, uint64_t ucode_data_hash,
               const uint32_t* ucode_dwords, size_t ucode_dword_count,
               std::endian ucode_source_endian)
    : shader_type_(shader_type), ucode_data_hash_(ucode_data_hash) {
  if (!label_addresses_.Initialize(ucode_dword_count / 3)) throw std::bad_alloc();
  // Snapshot directly into the final owner, without zero-filling it first.
  // Guest/file scratch bytes may change while asynchronous consumers use us.
  if (!ucode_data_.Allocate(ucode_dword_count)) throw std::bad_alloc();
  if (!ucode_dword_count) return;
  if (std::endian::native != ucode_source_endian) {
    // This kernel accepts aligned input too and selects AVX2 where available.
    // The generic dispatch otherwise sends aligned snapshots to the SSE loop.
    xe::copy_and_swap_32_unaligned(ucode_data_.data(), ucode_dwords,
                                   ucode_dword_count);
  } else {
    std::memcpy(ucode_data_.data(), ucode_dwords,
                sizeof(uint32_t) * ucode_dword_count);
  }
}

Shader::~Shader() {
  while (translations_) {
    xe_shader_variant_node* remaining = translations_;
    translations_ = nullptr;
    most_recent_translation_ = nullptr;
    while (xe_shader_variant_node* node = xe_shader_variant_drain_pop(&remaining)) {
      delete static_cast<Translation*>(node);
    }
  }
}

std::string Shader::Translation::GetTranslatedBinaryString() const {
  if (!translated_binary_.size()) return {};
  return std::string(reinterpret_cast<const char*>(translated_binary_.data()),
                     translated_binary_.size());
}

std::pair<std::filesystem::path, std::filesystem::path>
Shader::Translation::Dump(const std::filesystem::path& base_path,
                          const char* path_prefix) const {
  if (!is_valid()) {
    return std::make_pair(std::filesystem::path(), std::filesystem::path());
  }

  std::filesystem::path path = base_path;
  // Ensure target path exists.
  std::filesystem::path target_path = base_path;
  if (!target_path.empty()) {
    target_path = std::filesystem::absolute(target_path);
    std::filesystem::create_directories(target_path);
  }

  const char* type_extension =
      shader().type() == xenos::ShaderType::kVertex ? "vert" : "frag";

  std::filesystem::path binary_path =
      target_path / fmt::format("shader_{:016X}_{:016X}.{}.bin.{}",
                                shader().ucode_data_hash(), modification(),
                                path_prefix, type_extension);
  FILE* binary_file = filesystem::OpenFile(binary_path, "wb");
  if (binary_file) {
    fwrite(translated_binary_.data(), sizeof(*translated_binary_.data()),
           translated_binary_.size(), binary_file);
    fclose(binary_file);
  }

  std::filesystem::path disasm_path;
  if (!host_disassembly_.empty()) {
    disasm_path =
        target_path / fmt::format("shader_{:016X}_{:016X}.{}.{}",
                                  shader().ucode_data_hash(), modification(),
                                  path_prefix, type_extension);
    FILE* disasm_file = filesystem::OpenFile(disasm_path, "w");
    if (disasm_file) {
      fwrite(host_disassembly_.data(), sizeof(*host_disassembly_.data()),
             host_disassembly_.size(), disasm_file);
      fclose(disasm_file);
    }
  }

  return std::make_pair(std::move(binary_path), std::move(disasm_path));
}

Shader::Translation* Shader::GetOrCreateTranslation(uint64_t modification,
                                                    bool* is_new) {
  if (most_recent_translation_ && most_recent_translation_->modification() == modification) {
    if (is_new) *is_new = false;
    return most_recent_translation_;
  }
  if (Translation* existing = GetTranslation(modification)) {
    if (is_new) *is_new = false;
    return existing;
  }
  Translation* translation = CreateTranslationInstance(modification);
  if (!translation) {
    if (is_new) *is_new = false;
    return nullptr;
  }
  Translation* registered = static_cast<Translation*>(
      xe_shader_variant_insert(&translations_, translation));
  if (registered != translation) {
    // A reentrant factory may have registered the same key. Keep its identity.
    delete translation;
    most_recent_translation_ = registered;
    if (is_new) *is_new = false;
    return registered;
  }
  most_recent_translation_ = translation;
  if (is_new) *is_new = true;
  return translation;
}

void Shader::DestroyTranslation(uint64_t modification) {
  Translation* translation = static_cast<Translation*>(xe_shader_variant_remove(&translations_, modification));
  if (most_recent_translation_ == translation) most_recent_translation_ = nullptr;
  delete translation;
}

std::pair<std::filesystem::path, std::filesystem::path> Shader::DumpUcode(
    const std::filesystem::path& base_path) const {
  // Ensure target path exists.
  std::filesystem::path target_path = base_path;
  if (!target_path.empty()) {
    target_path = std::filesystem::absolute(target_path);
    std::filesystem::create_directories(target_path);
  }

  const char* type_extension =
      type() == xenos::ShaderType::kVertex ? "vert" : "frag";

  std::filesystem::path binary_path =
      target_path / fmt::format("shader_{:016X}.ucode.bin.{}",
                                ucode_data_hash(), type_extension);
  FILE* binary_file = filesystem::OpenFile(binary_path, "wb");
  if (binary_file) {
    fwrite(ucode_data().data(), sizeof(*ucode_data().data()),
           ucode_data().size(), binary_file);
    fclose(binary_file);
  }

  std::filesystem::path disasm_path;
  if (is_ucode_analyzed()) {
    disasm_path = target_path / fmt::format("shader_{:016X}.ucode.{}",
                                            ucode_data_hash(), type_extension);
    FILE* disasm_file = filesystem::OpenFile(disasm_path, "w");
    if (disasm_file) {
      fwrite(ucode_disassembly().data(), sizeof(*ucode_disassembly().data()),
             ucode_disassembly().size(), disasm_file);
      fclose(disasm_file);
    }
  }

  return std::make_pair(std::move(binary_path), std::move(disasm_path));
}

Shader::Translation* Shader::CreateTranslationInstance(uint64_t modification) {
  // Default implementation for simple cases like ucode disassembly.
  return new Translation(*this, modification);
}

}  //  namespace gpu
}  //  namespace xe
