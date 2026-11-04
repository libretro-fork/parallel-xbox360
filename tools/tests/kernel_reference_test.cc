/* C++ bridge exercises the actual kernel object/reference ABI. */
#include "xenia/kernel/xobject.h"
#include <retro_atomic.h>
#include <rthreads/rthreads.h>
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do { if (!(x)) std::abort(); } while (0)
static retro_atomic_int_t destroyed, ready, start;
class TestObject : public xe::kernel::XObject {
 public:
  explicit TestObject(bool check_marks = false)
      : XObject(Type::Event), check_marks_(check_marks) {}
  unsigned marks[6] = {};
  ~TestObject() override {
    if (check_marks_) for (unsigned mark : marks) CHECK(mark == 0x1234abcd);
    retro_atomic_fetch_add_int(&destroyed, 1);
  }
 private:
  bool check_marks_;
};
struct RetireInput { TestObject* object; unsigned id; };
static void retire(void* opaque) {
  auto* input = static_cast<RetireInput*>(opaque);
  input->object->marks[input->id] = 0x1234abcd;
  input->object->Release();
}
static void worker(void* opaque) {
  auto* object = static_cast<TestObject*>(opaque);
  retro_atomic_fetch_add_int(&ready, 1);
  while (!retro_atomic_load_acquire_int(&start)) {}
  for (unsigned i = 0; i < 100000; ++i) {
    auto owned = xe::kernel::retain_object(object);
    auto copied = owned;
    object->set_priority_increment(0x80000042);
    CHECK(object->priority_increment() == 0x80000042);
    CHECK(!retro_atomic_load_acquire_int(&destroyed));
  }
}
int main() {
  auto owned = xe::kernel::object_ref<TestObject>(new TestObject());
  sthread_t* threads[6];
  for (auto& thread : threads) {
    thread = sthread_create(worker, owned.get()); CHECK(thread);
  }
  while (retro_atomic_load_acquire_int(&ready) != 6) {}
  retro_atomic_store_release_int(&start, 1);
  for (auto* thread : threads) sthread_join(thread);
  CHECK(!retro_atomic_load_acquire_int(&destroyed));
  owned.reset();
  CHECK(retro_atomic_load_acquire_int(&destroyed) == 1);
  owned.reset(new TestObject(true));
  RetireInput inputs[6];
  for (unsigned i = 0; i < 6; ++i) {
    inputs[i] = {owned.get(), i};
    owned->Retain();
    threads[i] = sthread_create(retire, &inputs[i]); CHECK(threads[i]);
  }
  // The final release must see every marker before any join does.
  owned.reset();
  for (auto* thread : threads) sthread_join(thread);
  CHECK(retro_atomic_load_acquire_int(&destroyed) == 2);
  std::puts("PASS: 1.2 million shared kernel references, unsigned metadata and exactly-once destruction");
}
