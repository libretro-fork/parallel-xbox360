/* Typed lifetime/range glue only. Storage, insertion, membership and iteration
 * are owned by the C89 bitmap. This view is immutable after shader analysis. */
#ifndef XENIA_GPU_SHADER_LABELS_H_
#define XENIA_GPU_SHADER_LABELS_H_
#include <new>
#include "libretro/core_shader_labels.h"
namespace xe {
namespace gpu {
class ShaderLabels {
 public:
  class Iterator {
   public:
    Iterator(const xe_shader_labels* labels, size_t index)
        : labels_(labels), index_(index) {}
    uint32_t operator*() const { return uint32_t(index_); }
    Iterator& operator++() {
      index_ = index_ == SIZE_MAX ? SIZE_MAX : xe_shader_labels_next(labels_, index_ + 1);
      return *this;
    }
    bool operator==(const Iterator& other) const {
      return labels_ == other.labels_ && index_ == other.index_;
    }
    bool operator!=(const Iterator& other) const { return !(*this == other); }
   private:
    const xe_shader_labels* labels_;
    size_t index_;
  };
  ShaderLabels() = default;
  ~ShaderLabels() { xe_shader_labels_close(&labels_); }
  ShaderLabels(const ShaderLabels&) = delete;
  ShaderLabels& operator=(const ShaderLabels&) = delete;
  bool Initialize(size_t pairs) { return xe_shader_labels_init(&labels_, pairs) != 0; }
  void insert(uint32_t label) {
    if (xe_shader_labels_insert(&labels_, label) < 0) throw std::bad_alloc();
  }
  size_t size() const { return labels_.count; }
  bool empty() const { return labels_.count == 0; }
  Iterator begin() const { return Iterator(&labels_, xe_shader_labels_next(&labels_, 0)); }
  Iterator end() const { return Iterator(&labels_, SIZE_MAX); }
  Iterator find(uint32_t label) const {
    return Iterator(&labels_, xe_shader_labels_contains(&labels_, label) ? label : SIZE_MAX);
  }
  bool operator!=(const ShaderLabels& other) const {
    return !xe_shader_labels_equal(&labels_, &other.labels_);
  }
 private:
  xe_shader_labels labels_{};
};
}  // namespace gpu
}  // namespace xe
#endif
