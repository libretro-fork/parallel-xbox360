/* Exercise the real common EDRAM collection policy without a GPU. */
#include "xenia/gpu/render_target_cache.h"
#include "xenia/gpu/register_file.h"
#include "xenia/memory.h"
#include "libretro/core_gpu_retire.h"
#include <cstdio>
#include <cstdlib>
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
      : RenderTargetCache(registers,memory,nullptr,1,1) { InitializeCommon(); }
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
};
} }
int main() {
  xe::Memory memory;
  xe::gpu::RegisterFile registers;
  xe::gpu::RenderTargetCollectionTest test(registers,memory);
  test.Run();
  test.RunReuse();
  std::puts("PASS: ownership/fence lifetime; 20000 moving EDRAM bases reuse four images without map growth; incompatible shapes remain distinct");
  return 0;
}
