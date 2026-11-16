/* Typed native-owner/STL-reference boundary. Corpus and policy driver are C89.
 * Build separately against each revision's actual header. No core device or
 * frontend is created. Timing builds use the unmodified CRT operator new. */
#include "xenia/gpu/d3d12/pipeline_cache.h"
#include "pipeline_hash_index_fixture.h"
#include <new>
#include <cstdlib>
#include <cstring>
#ifdef XE_PIPELINE_INDEX_TRACK_NEW
void* operator new(size_t bytes) {
  void* p = xe_pipeline_index_test_allocate(bytes);
  if (!p) throw std::bad_alloc();
  return p;
}
void operator delete(void* p) noexcept {
  xe_pipeline_index_test_free(p);
}
void operator delete(void* p, size_t) noexcept {
  xe_pipeline_index_test_free(p);
}
#endif
namespace xe { namespace gpu { namespace d3d12 {
struct PipelinePublicationTest {
  using Owner = PipelineCache::Pipeline;
  using Key = PipelineCache::PipelineDescription;
  static_assert(sizeof(Key) == sizeof(xe_pipeline_index_key));
  static Owner* make(const xe_pipeline_index_key* key) {
    auto* owner = new Owner;
    std::memcpy(&owner->description.description, key, sizeof(Key));
    return owner;
  }
#ifdef XE_D3D12_INTRUSIVE_PIPELINE_INDEX
  static constexpr size_t kBuckets = PipelineCache::kPipelineIndexBuckets;
  static Owner* from_node(xe_pipeline_hash_node* node) {
    return PipelineCache::PipelineFromIndex(node);
  }
#endif
};
}}}
using Native = xe::gpu::d3d12::PipelinePublicationTest;
#ifdef XE_D3D12_INTRUSIVE_PIPELINE_INDEX
struct Context {
  xe_pipeline_hash_node* buckets[Native::kBuckets];
  xe_pipeline_hash_index index{};
  Context() {
    if (!xe_pipeline_hash_init(&index, buckets, Native::kBuckets)) throw std::bad_alloc();
  }
};
#else
using Context = std::unordered_multimap<uint64_t, Native::Owner*, xe::hash::IdentityHasher<uint64_t>>;
#endif
static void* open_context() { return new Context; }
static void clear_context(void* opaque) {
  auto* context = static_cast<Context*>(opaque);
#ifdef XE_D3D12_INTRUSIVE_PIPELINE_INDEX
  size_t cursor = 0;
  while (xe_pipeline_hash_drain(&context->index, &cursor)) {}
#else
  context->clear();
#endif
}
static void close_context(void* opaque) {
  if (opaque) { clear_context(opaque); delete static_cast<Context*>(opaque); }
}
static void* owner_new(const xe_pipeline_index_key* key) { return Native::make(key); }
static void owner_free(void* opaque) { delete static_cast<Native::Owner*>(opaque); }
static int insert_owner(void* opaque, uint64_t hash, void* value) {
  auto* context = static_cast<Context*>(opaque);
  auto* owner = static_cast<Native::Owner*>(value);
#ifdef XE_D3D12_INTRUSIVE_PIPELINE_INDEX
  return xe_pipeline_hash_insert(&context->index, &owner->index_node, hash);
#else
  context->emplace(hash, owner);
  return 1;
#endif
}
static void* find_owner(void* opaque, uint64_t hash, const void* query_owner) {
  auto* context = static_cast<Context*>(opaque);
  const Native::Key& typed_key = static_cast<const Native::Owner*>(query_owner)->description.description;
#ifdef XE_D3D12_INTRUSIVE_PIPELINE_INDEX
  for (auto* node = xe_pipeline_hash_first(&context->index, hash); node; node = xe_pipeline_hash_next(node)) {
    auto* owner = Native::from_node(node);
#ifdef XE_PIPELINE_INDEX_IGNORE_KEY
    return owner;
#else
    if (owner->description.description == typed_key) return owner;
#endif
  }
#else
  auto range = context->equal_range(hash);
  for (auto it = range.first; it != range.second; ++it) {
    if (it->second->description.description == typed_key) return it->second;
  }
#endif
  return nullptr;
}
static size_t owner_count(void* opaque) {
#ifdef XE_D3D12_INTRUSIVE_PIPELINE_INDEX
  return static_cast<Context*>(opaque)->index.count;
#else
  return static_cast<Context*>(opaque)->size();
#endif
}
static size_t bucket_bytes(void* opaque) {
#ifdef XE_D3D12_INTRUSIVE_PIPELINE_INDEX
  (void)opaque;
  return sizeof(Context::buckets);
#else
  return static_cast<Context*>(opaque)->bucket_count() * sizeof(void*);
#endif
}
static void allocation_reset() {
#ifdef XE_PIPELINE_INDEX_TRACK_NEW
  xe_pipeline_index_test_reset();
#endif
}
static size_t allocation_count() {
#ifdef XE_PIPELINE_INDEX_TRACK_NEW
  return xe_pipeline_index_test_count();
#else
  return (size_t)-1;
#endif
}
static size_t live_allocation_count() {
#ifdef XE_PIPELINE_INDEX_TRACK_NEW
  return xe_pipeline_index_test_live();
#else
  return (size_t)-1;
#endif
}
extern "C" __declspec(dllexport) const xe_pipeline_index_fixture* xe_pipeline_index_fixture_api() {
  static const xe_pipeline_index_fixture api = {
      1, sizeof(xe_pipeline_index_fixture),
#ifdef XE_D3D12_INTRUSIVE_PIPELINE_INDEX
      1,
#else
      0,
#endif
#ifdef XE_PIPELINE_INDEX_TRACK_NEW
      1,
#else
      0,
#endif
      sizeof(Native::Owner), open_context, clear_context, close_context,
      owner_new, owner_free, insert_owner, find_owner, owner_count, bucket_bytes,
      allocation_reset, allocation_count, live_allocation_count};
  return &api;
}
