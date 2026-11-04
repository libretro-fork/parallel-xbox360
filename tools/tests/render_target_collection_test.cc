/* Exercise the real common EDRAM collection policy without a GPU. */
#include "xenia/gpu/render_target_cache.h"
#include "xenia/gpu/register_file.h"
#include "xenia/memory.h"
#include "libretro/core_gpu_retire.h"
#include <cstdio>
#include <cstdlib>
#include <new>
static bool count_query_allocations;
static size_t query_allocations;
void* operator new(size_t size) {
  void* allocation = std::malloc(size ? size : 1);
  if (!allocation) throw std::bad_alloc();
  if (count_query_allocations) ++query_allocations;
  return allocation;
}
void operator delete(void* allocation) noexcept { std::free(allocation); }
void operator delete(void* allocation, size_t) noexcept { std::free(allocation); }
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); std::exit(1); } } while (0)
namespace xe { namespace gpu {
class RenderTargetCollectionTest final : public RenderTargetCache {
  struct Target final : RenderTarget {
    unsigned* destroyed;
    xe_gpu_retire_node retirement = {};
    Target(RenderTargetKey key, unsigned* count) : RenderTarget(key), destroyed(count) {}
    ~Target() { ++*destroyed; }
  };
  unsigned destroyed_ = 0, retired_ = 0, created_ = 0;
  xe_gpu_retire_queue retirement_ = {};
  static void Release(void* target) { delete static_cast<Target*>(target); }
 public:
  RenderTargetCollectionTest(const RegisterFile& registers, const Memory& memory)
      : RenderTargetCache(registers,memory,nullptr,2,2) { InitializeCommon(); }
  Path GetPath() const override { return Path::kHostRenderTargets; }
  uint32_t GetMaxRenderTargetWidth() const override { return 16384; }
  uint32_t GetMaxRenderTargetHeight() const override { return 16384; }
  bool IsGammaFormatHostStorageSeparate() const override { return false; }
  bool IsHostDepthEncodingDifferent(xenos::DepthRenderTargetFormat) const override { return false; }
  RenderTarget* CreateRenderTarget(RenderTargetKey key) override {
    ++created_;
    return new Target(key,&destroyed_);
  }
  void RetireCachedRenderTarget(RenderTarget* target) override {
    Target* item = static_cast<Target*>(target);
    CHECK(xe_gpu_retire_append(&retirement_,&item->retirement,42,Release,item));
    ++retired_;
  }
  void Run() {
    RenderTargetKey keys[6];
    RenderTarget* owner;
    size_t buckets;
    unsigned i;
    CHECK(!PrepareRenderTargetCollection());
    for(i=0;i<5;++i) {
      keys[i].key=i+1;
      render_targets_.emplace(keys[i],CreateRenderTarget(keys[i]));
    }
    keys[5].key=6;
    render_targets_.emplace(keys[5],nullptr);
    OwnershipRange& range=ownership_ranges_.begin()->second;
    range.render_target=keys[0];
    range.depth_bits_target=keys[1];
    range.host_depth_render_target_unorm24=keys[2];
    range.host_depth_render_target_float24=keys[3];
    owner=render_targets_.find(keys[0])->second;
    last_update_accumulated_render_targets_[0]=render_targets_.find(keys[4])->second;
    last_update_used_render_targets_[0]=last_update_accumulated_render_targets_[0];
    are_accumulated_render_targets_valid_=true;
    CHECK(PrepareRenderTargetCollection());
    CHECK(are_accumulated_render_targets_valid_ && last_update_used_render_targets_[0]);
    CHECK(retired_==0 && destroyed_==0 && render_targets_.size()==6);
    CollectPreparedRenderTargets();
    CHECK(retired_==1 && destroyed_==0 && render_targets_.size()==4);
    CHECK(!are_accumulated_render_targets_valid_ && !last_update_used_render_targets_[0]);
    CHECK(!last_update_accumulated_render_targets_[0]);
    CHECK(!xe_gpu_retire_collect(&retirement_,41) && destroyed_==0);
    CHECK(xe_gpu_retire_collect(&retirement_,42)==1 && destroyed_==1);
    /* No orphan: inspection/clear must not discard the current CPU binding,
     * change ownership, rebuild hash buckets or retire any of the four forms. */
    last_update_accumulated_render_targets_[0]=owner;
    last_update_used_render_targets_[0]=owner;
    are_accumulated_render_targets_valid_=true;
    buckets=render_targets_.bucket_count();
    for(i=0;i<10000;++i) {
      CHECK(!PrepareRenderTargetCollection());
      ClearCache();
      CHECK(are_accumulated_render_targets_valid_);
      CHECK(last_update_accumulated_render_targets_[0]==owner);
      CHECK(last_update_used_render_targets_[0]==owner);
      CHECK(retired_==1 && destroyed_==1 && render_targets_.size()==4);
      CHECK(render_targets_.bucket_count()==buckets);
    }
    range=OwnershipRange(xenos::kEdramTileCount,RenderTargetKey(),RenderTargetKey(),RenderTargetKey());
    ClearCache();
    CHECK(retired_==5 && destroyed_==1 && render_targets_.empty());
    CHECK(xe_gpu_retire_collect(&retirement_,42)==4 && destroyed_==5);
  }
  void RunReuse() {
    RenderTargetKey key;
    /* A reuse search must preserve every authoritative ownership encoding,
     * not only the ordinary color/depth owner. */
    RenderTargetKey owners[4];
    for (unsigned i = 0; i < 4; ++i) {
      key.key = 0;
      key.base_tiles = i;
      key.pitch_tiles_at_32bpp = 16;
      SetResourceFrame(i);
      CHECK(GetOrCreateRenderTarget(key));
      owners[i] = key;
    }
    OwnershipRange& owned = ownership_ranges_.begin()->second;
    owned.render_target = owners[0];
    owned.depth_bits_target = owners[1];
    owned.host_depth_render_target_unorm24 = owners[2];
    owned.host_depth_render_target_float24 = owners[3];
    unsigned all_owned = created_;
    SetResourceFrame(100);
    key.base_tiles = 800;
    CHECK(GetOrCreateRenderTarget(key));
    CHECK(created_ == all_owned + 1);
    owned = OwnershipRange(xenos::kEdramTileCount,
        RenderTargetKey(), RenderTargetKey(), RenderTargetKey());
    ClearCache();
    xe_gpu_retire_collect(&retirement_,42);
    unsigned initial = created_;
    uint64_t identity = 0;
    size_t buckets = 0;
    for (unsigned frame = 0; frame < 20000; ++frame) {
      SetResourceFrame(frame);
      key.key = 0;
      key.base_tiles = frame % 2048;
      key.pitch_tiles_at_32bpp = 16;
      RenderTarget* target = GetOrCreateRenderTarget(key);
      CHECK(target && target->key() == key && target->identity());
      if (frame == 4) {
        identity = target->identity();
        buckets = render_targets_.bucket_count();
      }
      if (frame > 4) CHECK(render_targets_.bucket_count() == buckets);
      if (frame == 8) CHECK(target->identity() == identity);
      CHECK(created_ - initial <= 4);
      /* The just-used image owns guest data; each subsequent request must
       * preserve it until this new owner is published. */
      ownership_ranges_.begin()->second.render_target = key;
    }
    CHECK(render_targets_.size() == 4 && created_ - initial == 4);
    /* An older orphan in a CPU binding snapshot cannot be borrowed. */
    RenderTarget* protected_target = render_targets_.begin()->second;
    last_update_used_render_targets_[0] = protected_target;
    last_update_accumulated_render_targets_[0] = protected_target;
    SetResourceFrame(30000);
    key.base_tiles = 2000;
    RenderTarget* replacement = GetOrCreateRenderTarget(key);
    CHECK(replacement != protected_target);
    /* Equal dimensions alone do not permit depth/format/MSAA/scale aliasing. */
    unsigned before = created_;
    key.is_depth = 1;
    CHECK(GetOrCreateRenderTarget(key));
    CHECK(created_ == before + 1);
    key.is_depth = 0;
    key.msaa_samples = xenos::MsaaSamples::k4X;
    CHECK(GetOrCreateRenderTarget(key));
    CHECK(created_ == before + 2);
    key.msaa_samples = xenos::MsaaSamples::k1X;
    key.scale_native = 1;
    CHECK(GetOrCreateRenderTarget(key));
    CHECK(created_ == before + 3);
    key.scale_native = 0;
    key.resource_format = 1;
    CHECK(GetOrCreateRenderTarget(key));
    CHECK(created_ == before + 4);
    /* Automatic collection retains recent orphans; explicit clear does not. */
    CHECK(PrepareRenderTargetCollection(8));
    CollectPreparedRenderTargets();
    CHECK(render_targets_.find(key) != render_targets_.end());
    ownership_ranges_.begin()->second = OwnershipRange(xenos::kEdramTileCount,
        RenderTargetKey(), RenderTargetKey(), RenderTargetKey());
    ClearCache();
    xe_gpu_retire_collect(&retirement_,42);
    CHECK(render_targets_.empty() && destroyed_ == created_);
  }
  void RunResolveQueries() {
    RenderTargetKey native, scaled;
    native.key = 0;
    native.pitch_tiles_at_32bpp = 32;
    native.scale_native = 1;
    scaled = native;
    scaled.scale_native = 0;
    scaled.base_tiles = 16;
    CHECK(GetOrCreateRenderTarget(native));
    CHECK(GetOrCreateRenderTarget(scaled));
    ownership_ranges_.clear();
    ownership_ranges_.emplace(0,OwnershipRange(8,native,{},{}));
    ownership_ranges_.emplace(8,OwnershipRange(16,{},{},{}));
    ownership_ranges_.emplace(16,OwnershipRange(24,scaled,{},{}));
    ownership_ranges_.emplace(24,OwnershipRange(32,{},{},{}));
    ownership_ranges_.emplace(32,OwnershipRange(xenos::kEdramTileCount,native,{},{}));
    std::vector<ResolveCopyDumpRectangle> rectangles;
    unsigned results = 0;
    for (unsigned i = 0; i < 10000; ++i) {
      draw_util::ResolveInfo info = {};
      info.rb_copy_control.copy_src_select = (i & 8) ? 4 : 0;
      draw_util::ResolveEdramInfo& edram = info.IsCopyingDepth()
          ? info.depth_edram_info : info.color_edram_info;
      edram.base_tiles = i % xenos::kEdramTileCount;
      edram.pitch_tiles = 32;
      edram.msaa_samples = xenos::MsaaSamples((i >> 4) % 3);
      edram.format_is_64bpp = (i >> 6) & 1;
      info.coordinate_info.width_div_8 = ((i >> 2) % 8 + 1) * 10;
      info.height_div_8 = (i % 4 + 1) * 2;
      uint32_t base, width, rows, pitch;
      info.GetCopyEdramTileSpan(base,width,rows,pitch);
      GetResolveCopyRectanglesToDump(base,width,rows,pitch,rectangles);
      bool expected_native = !rectangles.empty();
      uint64_t tiles = 0;
      DirectResolveEligibility expected = rectangles.empty()
          ? DirectResolveEligibility::kNoOwnership
          : DirectResolveEligibility::kEligible;
      for (const auto& rectangle : rectangles) {
        RenderTargetKey key = rectangle.render_target->key();
        expected_native &= key.scale_native != 0;
        if (key.is_depth != uint32_t(info.IsCopyingDepth()) ||
            key.msaa_samples != edram.msaa_samples ||
            uint32_t(key.Is64bpp()) != edram.format_is_64bpp)
          expected = DirectResolveEligibility::kSourceLayoutMismatch;
        tiles += rectangle.GetTileCount(width);
      }
      if (expected == DirectResolveEligibility::kEligible &&
          tiles != uint64_t(width)*rows)
        expected = DirectResolveEligibility::kPartialOwnership;
      count_query_allocations = true;
      bool actual_native = IsResolveSourceNativeOnly(base,width,rows,pitch);
      DirectResolveEligibility actual = GetDirectResolveEligibility(info,
          draw_util::ResolveCopyShaderIndex::kFast32bpp1x2xMSAA);
      count_query_allocations = false;
      CHECK(actual_native == expected_native && actual == expected);
      CHECK(query_allocations == 0);
      results |= 1u << unsigned(actual);
    }
    CHECK(results & (1u << unsigned(DirectResolveEligibility::kEligible)));
    CHECK(results & (1u << unsigned(DirectResolveEligibility::kNoOwnership)));
    CHECK(results & (1u << unsigned(DirectResolveEligibility::kSourceLayoutMismatch)));
    CHECK(results & (1u << unsigned(DirectResolveEligibility::kPartialOwnership)));
    ownership_ranges_.clear();
    ownership_ranges_.emplace(0,OwnershipRange(xenos::kEdramTileCount,{},{},{}));
    ClearCache();
    xe_gpu_retire_collect(&retirement_,42);
    CHECK(destroyed_ == created_);
  }
};
} }
int main() {
  xe::Memory memory;
  xe::gpu::RegisterFile registers;
  xe::gpu::RenderTargetCollectionTest test(registers,memory);
  test.Run();
  test.RunReuse();
  test.RunResolveQueries();
  std::puts("PASS: ownership/fence lifetime; 20000 moving EDRAM bases reuse four images without map growth; incompatible shapes remain distinct");
  std::puts("PASS: 10000 resolve cases match collected-rectangle semantics with zero query allocations");
  return 0;
}
