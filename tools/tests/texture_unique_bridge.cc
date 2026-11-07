/* Actual batch/watch ownership; backend records dispatches without a GPU. */
#include "xenia/gpu/texture_cache.h"
#include "xenia/base/logging.h"
#include "xenia/base/profiling.h"
#include <new>
class UniqueMemory final : public xe::gpu::SharedMemory {
 public:
  explicit UniqueMemory(xe::Memory& memory):SharedMemory(memory) {}
  bool initialize() {return InitializeCommon();}
  bool UploadRanges(xe_upload_range* ranges,uint32_t count) override {
    for(uint32_t i=0;i<count;++i)
      MakeRangeValid(ranges[i].first<<page_size_log2(),ranges[i].second<<page_size_log2(),false);
    return true;
  }
};
class UniqueCache final : public xe::gpu::TextureCache {
 public:
  struct Item final : Texture {unsigned id;Item(UniqueCache& c,const TextureKey& k,unsigned i):Texture(c,k),id(i) {}};
  unsigned calls=0,ids[64]={},masks[64]={};bool success=true,invalidate=false,span_unchanged=false;
  UniqueCache(const xe::gpu::RegisterFile& r,UniqueMemory& m):TextureCache(r,m,nullptr,1,1) {}
  uint32_t GetHostFormatSwizzle(TextureKey) const override {return 0;}
  uint32_t GetMaxHostTextureWidthHeight(xe::gpu::xenos::DataDimension) const override {return 16384;}
  uint32_t GetMaxHostTextureDepthOrArraySize(xe::gpu::xenos::DataDimension) const override {return 2048;}
  std::unique_ptr<Texture> CreateTexture(TextureKey) override {return nullptr;}
  bool LoadTextureDataFromResidentMemoryImpl(Texture& texture,bool base,bool mips) override {
    if(calls>=64) return false;
    ids[calls]=static_cast<Item&>(texture).id;masks[calls++]=(base?1:0)|(mips?2:0);
    if(invalidate) shared_memory().MemoryInvalidationCallback(texture.key().base_page<<12,texture.GetGuestBaseSize(),true);
    return success;
  }
  Item* make(unsigned id) {
    TextureKey key;key.base_page=256+16*(id==1?0:id);key.mip_page=8192+16*(id==1?0:id);
    key.dimension=xe::gpu::xenos::DataDimension::k2DOrStacked;
    key.width_minus_1=key.height_minus_1=63;key.pitch=2;key.mip_max_level=3;
    key.format=xe::gpu::xenos::TextureFormat::k_8_8_8_8;key.is_valid=1;key.signed_separate=id==1;
    return new Item(*this,key,id);
  }
  void batch(Item* const* owners,const unsigned* indices,unsigned count) {
    Texture* borrowed[64];for(unsigned i=0;i<count;++i) borrowed[i]=owners[indices[i]];
    calls=0;LoadTexturesData(borrowed,count);
    span_unchanged=true;
    for(unsigned i=0;i<count;++i) if(borrowed[i]!=owners[indices[i]]) span_unchanged=false;
  }
};
struct UniqueFixture {
  xe::Memory memory;xe::gpu::RegisterFile registers;UniqueMemory shared;UniqueCache cache;
  UniqueCache::Item* items[64]={};
  UniqueFixture():shared(memory),cache(registers,shared) {}
  ~UniqueFixture() {for(auto* item:items) delete item;}
};
extern "C" void* xe_unique_native_new(void) {
  xe::InitializeLogging("texture_unique_test");xe::Profiler::Initialize();
  auto* f=new(std::nothrow) UniqueFixture;if(!f) return nullptr;
  if(!f->memory.Initialize() || !f->shared.initialize()) {delete f;return nullptr;}
  for(unsigned i=0;i<64;++i) f->items[i]=f->cache.make(i);
  return f;
}
extern "C" void xe_unique_native_free(void* p) {delete static_cast<UniqueFixture*>(p);xe::ShutdownLogging();}
extern "C" void xe_unique_native_dirty(void* p,unsigned id,unsigned mask) {
  auto* f=static_cast<UniqueFixture*>(p);auto& t=*f->items[id];
  if(mask&1) f->shared.RangeWrittenByGpu(t.key().base_page<<12,t.GetGuestBaseSize());
  if(mask&2) f->shared.RangeWrittenByGpu(t.key().mip_page<<12,t.GetGuestMipsSize());
}
extern "C" unsigned xe_unique_native_state(void* p,unsigned id) {return static_cast<UniqueFixture*>(p)->items[id]->outdated_mask_lockless();}
extern "C" unsigned xe_unique_native_batch(void* p,const unsigned* ids,unsigned count,int success,int invalidate,unsigned* order,unsigned* masks) {
  auto* f=static_cast<UniqueFixture*>(p);f->cache.success=success!=0;f->cache.invalidate=invalidate!=0;
  f->cache.batch(f->items,ids,count);
  for(unsigned i=0;i<f->cache.calls;++i) {order[i]=f->cache.ids[i];masks[i]=f->cache.masks[i];}
  return f->cache.calls;
}
extern "C" int xe_unique_native_span_unchanged(void* p) {
  return static_cast<UniqueFixture*>(p)->cache.span_unchanged;
}
