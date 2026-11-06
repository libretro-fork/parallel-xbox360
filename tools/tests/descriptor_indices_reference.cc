/* Boundary/reference only: old free vector/FIFO, real C89 link storage and
 * command-owner batch transfer. No D3D12 device or native driver is created. */
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <deque>
#include <vector>
static size_t allocated, freed;
static void* test_alloc(size_t bytes) { void* p = std::malloc(bytes); if (p) ++allocated; return p; }
static void test_free(void* p) { if (p) ++freed; std::free(p); }
#define XE_DESCRIPTOR_INDEX_ALLOC test_alloc
#define XE_DESCRIPTOR_INDEX_FREE test_free
#include "core_descriptor_indices.h"
struct Used { uint32_t index; uint64_t submission; };
struct Owner { uint64_t submission; xe_descriptor_index_batch batch; };
struct SyntheticTexture {
  xe_descriptor_indices* pool; uint32_t index = UINT32_MAX;
  ~SyntheticTexture() { if (index != UINT32_MAX && !xe_descriptor_index_release(pool, index)) std::abort(); }
};
struct SyntheticContext {
  xe_descriptor_indices_owner pool;
  SyntheticTexture texture{&pool};
  SyntheticContext() {
    if (!xe_descriptor_indices_init(&pool, 32, 4)) std::abort();
    texture.index = xe_descriptor_index_take(&pool);
  }
};
int main() {
  { SyntheticContext context; }
  if (allocated != freed) return 1;
  xe_descriptor_indices_owner pool;
  xe_descriptor_index_batch current;
  Owner rows[8]{};
  std::deque<Owner*> owners;
  std::deque<Used> used;
  std::vector<uint32_t> free;
  uint32_t persistent[128], persistent_count = 0, old_allocated = 8, random = 49177;
  uint64_t events = 0, retries = 0;
  auto take_old = [&]() {
    if (!free.empty()) { uint32_t i = free.back(); free.pop_back(); return i; }
    return old_allocated < 512 ? old_allocated++ : UINT32_MAX;
  };
  auto collect = [&](uint64_t completed) {
    while (!used.empty() && used.front().submission <= completed) {
      free.push_back(used.front().index); used.pop_front();
    }
    while (!owners.empty() && owners.front()->submission <= completed) {
      if (!xe_descriptor_index_batch_recycle(&pool, &owners.front()->batch)) std::abort();
      owners.pop_front();
    }
    if (xe_descriptor_indices_available(&pool) != 512 - old_allocated + free.size()) std::abort();
  };
  if (!xe_descriptor_indices_init(&pool, 512, 8)) return 2;
  xe_descriptor_index_batch_clear(&current);
  for (auto& row : rows) xe_descriptor_index_batch_clear(&row.batch);
  for (uint64_t submission = 1; submission <= 20000; ++submission) {
    collect(submission > 4 ? submission - 4 : 0);
    for (unsigned op = 0; op < 20; ++op) {
      random ^= random << 13; random ^= random >> 17; random ^= random << 5;
      if (persistent_count && (random & 1)) {
        unsigned at = random % persistent_count;
        uint32_t id = persistent[at]; persistent[at] = persistent[--persistent_count];
        free.push_back(id); if (!xe_descriptor_index_release(&pool, id)) return 3;
      } else if (persistent_count < 128) {
        uint32_t expected = take_old(), actual = xe_descriptor_index_take(&pool);
        if (expected != actual) return 4;
        if (actual != UINT32_MAX) persistent[persistent_count++] = actual;
      }
      ++events;
    }
    unsigned count = (random >> 7) & 63;
    size_t available = 512 - old_allocated + free.size();
    if (available != xe_descriptor_indices_available(&pool)) return 5;
    if (count <= available) for (unsigned draw = 0; draw < count; ++draw) {
      uint32_t expected = take_old(), actual = xe_descriptor_index_take_one_use(&pool, &current);
      if (expected != actual) return 6;
      used.push_back({expected, submission}); ++events;
    }
    auto& row = rows[submission % 8];
    if (!(submission % 37)) { // Failed submission-owner allocation: retry before any batch move.
      uint32_t pending = current.count;
      collect(submission > 4 ? submission - 4 : 0);
      if (current.count != pending) return 7;
      ++retries;
    }
    if (!xe_descriptor_index_batch_move(&row.batch, &current)) return 8;
    row.submission = submission; owners.push_back(&row);
    if (!(submission % 997)) {
      collect(UINT64_MAX);
      for (unsigned i = 0; i < persistent_count; ++i) {
        free.push_back(persistent[i]); if (!xe_descriptor_index_release(&pool, persistent[i])) return 9;
      }
      persistent_count = 0; free.clear(); old_allocated = 8;
      xe_descriptor_indices_destroy(&pool);
      if (!xe_descriptor_indices_init(&pool, 512, 8)) return 10;
    }
  }
  collect(UINT64_MAX);
  xe_descriptor_indices_destroy(&pool);
  if (!retries || allocated != freed) return 11;
  std::printf("Descriptor index reference: %llu persistent/transient events match vector/FIFO across 20 resets and %llu pending-submission retries; late texture release precedes pool destruction\n",
      (unsigned long long)events, (unsigned long long)retries);
  return 0;
}
