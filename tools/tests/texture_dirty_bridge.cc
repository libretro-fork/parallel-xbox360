/* Actual texture watch ABI without a GPU/frontend/JIT. */
#include "xenia/gpu/texture_cache.h"
#include "xenia/base/logging.h"
#include "xenia/base/profiling.h"
#include <new>
class DirtyMemory:public xe::gpu::SharedMemory {
 public:
  explicit DirtyMemory(xe::Memory& m):SharedMemory(m) {}
  bool initialize(){return InitializeCommon();}
  bool UploadRanges(xe_upload_range*,uint32_t) override{return false;}
};
class DirtyCache:public xe::gpu::TextureCache {
 public:
  struct Item:Texture {Item(DirtyCache& c,const TextureKey& k):Texture(c,k) {}};
  DirtyCache(const xe::gpu::RegisterFile& r,DirtyMemory& m):TextureCache(r,m,nullptr,1,1) {}
  uint32_t GetHostFormatSwizzle(TextureKey) const override{return 0;}
  uint32_t GetMaxHostTextureWidthHeight(xe::gpu::xenos::DataDimension) const override{return 16384;}
  uint32_t GetMaxHostTextureDepthOrArraySize(xe::gpu::xenos::DataDimension) const override{return 2048;}
  std::unique_ptr<Texture> CreateTexture(TextureKey) override{return nullptr;}
  bool LoadTextureDataFromResidentMemoryImpl(Texture&,bool,bool) override{return false;}
  Item* make(){
    TextureKey k;k.base_page=256;k.mip_page=512;k.dimension=xe::gpu::xenos::DataDimension::k2DOrStacked;
    k.width_minus_1=k.height_minus_1=63;k.pitch=2;k.mip_max_level=3;k.format=xe::gpu::xenos::TextureFormat::k_8_8_8_8;
    k.is_valid=1;return new Item(*this,k);
  }
};
struct DirtyFixture {
  xe::Memory memory;xe::gpu::RegisterFile registers;DirtyMemory shared;DirtyCache cache;
  std::unique_ptr<DirtyCache::Item> texture;
  DirtyFixture():shared(memory),cache(registers,shared) {}
};
extern "C" void *xe_dirty_native_new(void) {
  static bool initialized=false;
  if(!initialized){xe::InitializeLogging("texture_dirty_test");xe::Profiler::Initialize();initialized=true;}
  auto* f=new(std::nothrow) DirtyFixture;
  if(!f) return nullptr;
  if(!f->memory.Initialize() || !f->shared.initialize()){delete f;return nullptr;}
  f->texture.reset(f->cache.make());return f;
}
extern "C" void xe_dirty_native_free(void *p){delete static_cast<DirtyFixture*>(p);}
extern "C" unsigned xe_dirty_native_load(void *p){return static_cast<DirtyFixture*>(p)->texture->outdated_mask_lockless();}
extern "C" void xe_dirty_native_mark(void *p,unsigned mask){
  auto* f=static_cast<DirtyFixture*>(p);
  if(mask&1) f->shared.RangeWrittenByGpu(256u<<12,f->texture->GetGuestBaseSize());
  if(mask&2) f->shared.RangeWrittenByGpu(512u<<12,f->texture->GetGuestMipsSize());
}
extern "C" int xe_dirty_native_refresh(void *p){
  xe::global_critical_region region;auto lock=region.Acquire();
  return static_cast<DirtyFixture*>(p)->texture->MakeUpToDateAndWatch(lock);
}
