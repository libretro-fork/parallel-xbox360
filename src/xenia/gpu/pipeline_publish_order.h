/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_PIPELINE_PUBLISH_ORDER_H_
#define XENIA_GPU_PIPELINE_PUBLISH_ORDER_H_

#include <cstddef>
#include <memory>
#include <new>
#include <utility>
#include <type_traits>
#include "libretro/core_pipeline_order.h"

namespace xe {
namespace gpu {

// Minimal object-lifetime adapter around the C89 reusable publication slots.
// Reservation has one producer; finished objects may arrive from many workers.
template <typename T>
class PipelinePublishOrder {
 public:
  ~PipelinePublishOrder() {
    xe_pipeline_order_reset(order_.get(), &Destroy, nullptr);
  }

  using StoreCallback = void (*)(void*, T&);
  void Initialize(StoreCallback store, void* opaque) {
    static_assert(alignof(T) <= alignof(std::max_align_t));
    static_assert(std::is_nothrow_move_constructible<T>::value);
    store_ = store;
    opaque_ = opaque;
    if (!order_) {
      order_.reset(xe_pipeline_order_new(4096, sizeof(T), &Store, this));
      if (!order_) throw std::bad_alloc();
    }
  }

  // Reserve outside locks needed by completion workers. A saturated window
  // waits for earlier publication, never grows or drops a pipeline.
  size_t NextSequence() { return xe_pipeline_order_reserve(order_.get()); }

  void Publish(size_t sequence, T value) {
    if (!sequence) {
      store_(opaque_, value);
      return;
    }
    void* slot = xe_pipeline_order_payload(order_.get(), sequence);
    new (slot) T(std::move(value));
    xe_pipeline_order_complete(order_.get(), sequence);
  }

  // After workers join, destroy unpublished handles directly in their slots.
  template <typename F>
  void Reset(F&& discard) {
    using Callback = std::remove_reference_t<F>;
    xe_pipeline_order_reset(order_.get(), [](void* opaque, void* payload) {
      auto* value = static_cast<T*>(payload);
      (*static_cast<Callback*>(opaque))(*value);
      value->~T();
    }, &discard);
  }

 private:
  static void Destroy(void*, void* payload) {
    static_cast<T*>(payload)->~T();
  }
  static void Store(void* opaque, void* payload) {
    auto* self = static_cast<PipelinePublishOrder*>(opaque);
    auto* value = static_cast<T*>(payload);
    self->store_(self->opaque_, *value);
    value->~T();
  }
  StoreCallback store_ = nullptr;
  void* opaque_ = nullptr;
  std::unique_ptr<xe_pipeline_order, decltype(&xe_pipeline_order_free)>
      order_{nullptr, xe_pipeline_order_free};
};

}  // namespace gpu
}  // namespace xe
#endif  // XENIA_GPU_PIPELINE_PUBLISH_ORDER_H_
