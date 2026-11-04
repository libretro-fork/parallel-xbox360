/* Only the C++ object-lifetime adapter is tested here; scheduling is C89. */
#include "xenia/gpu/pipeline_publish_order.h"
#include <cstdio>
#include <cstdlib>
struct Value {
  static int alive;
  size_t id;
  explicit Value(size_t value) : id(value) { ++alive; }
  Value(Value&& other) noexcept : id(other.id) { other.id = 0; ++alive; }
  Value(const Value&) = delete;
  ~Value() { --alive; }
};
int Value::alive = 0;
#define CHECK(x) do { if (!(x)) std::abort(); } while (0)
int main() {
  size_t expected = 1, discarded = 0;
  {
    xe::gpu::PipelinePublishOrder<Value> order;
    order.Initialize([](void* opaque, Value& value) {
      CHECK(value.id == (*static_cast<size_t*>(opaque))++);
    }, &expected);
    size_t first = order.NextSequence(), second = order.NextSequence();
    order.Publish(second, Value(2));
    CHECK(expected == 1 && Value::alive == 1);
    order.Publish(first, Value(1));
    CHECK(expected == 3 && Value::alive == 0);
    order.Reset([&](Value&) { ++discarded; });
    CHECK(discarded == 0);
    first = order.NextSequence(); second = order.NextSequence();
    CHECK(first == 1 && second == 2);
    order.Publish(second, Value(2));
    order.Reset([&](Value& value) {
      CHECK(value.id == 2 && Value::alive == 1);
      ++discarded;
    });
    CHECK(discarded == 1);
    CHECK(Value::alive == 0);
    /* Warm-up publications bypass ordering and are never retained. */
    order.Publish(0, Value(3));
    CHECK(expected == 4 && Value::alive == 0);
    order.NextSequence(); second = order.NextSequence();
    order.Publish(second, Value(5));
    CHECK(Value::alive == 1);
  }
  CHECK(Value::alive == 0);
  std::puts("PASS: production publication adapter move-only object lifetime, failure-gap reset and unordered warm-up");
}
