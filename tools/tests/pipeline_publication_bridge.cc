/* Actual native entry/header ABI only; no device, frontend or pipeline API. */
#ifndef XE_PIPELINE_VULKAN_ONLY
#include "xenia/gpu/d3d12/pipeline_cache.h"
#endif
#include "xenia/gpu/vulkan/vulkan_pipeline_cache.h"
#include <type_traits>
#include <utility>
#ifndef XE_PIPELINE_VULKAN_ONLY
namespace xe {namespace gpu {namespace d3d12 {
struct PipelinePublicationTest {
  static PipelineCache::Pipeline *make(){return new PipelineCache::Pipeline;}
  static PipelineCache::Pipeline *cast(void *p){return static_cast<PipelineCache::Pipeline*>(p);}
  static xe_pipeline_publication *state(void *p){return &cast(p)->publication;}
  static void destroy(void *p){delete cast(p);}
  static unsigned size(){return sizeof(PipelineCache::Pipeline);}
  static bool noncopyable(){return !std::is_copy_constructible<PipelineCache::Pipeline>::value;}
};
}}}
using NativeD3D=xe::gpu::d3d12::PipelinePublicationTest;
#endif
using NativeVK=xe::gpu::vulkan::VulkanPipelineCache;
extern "C" void *xe_pipeline_native_new(unsigned backend){
  if(backend) return new NativeVK::Pipeline(
      reinterpret_cast<const NativeVK::PipelineLayoutProvider*>(uintptr_t(0x3000)));
#ifndef XE_PIPELINE_VULKAN_ONLY
  return NativeD3D::make();
#else
  return nullptr;
#endif
}
extern "C" void xe_pipeline_native_free(unsigned backend,void *p){
  if(backend) delete static_cast<NativeVK::Pipeline*>(p);
#ifndef XE_PIPELINE_VULKAN_ONLY
  else NativeD3D::destroy(p);
#endif
}
extern "C" xe_pipeline_publication *xe_pipeline_native_state(unsigned backend,void *p){
  if(backend) return &static_cast<NativeVK::Pipeline*>(p)->publication;
#ifndef XE_PIPELINE_VULKAN_ONLY
  return NativeD3D::state(p);
#else
  return nullptr;
#endif
}
extern "C" uint64_t xe_pipeline_native_draw(unsigned backend,void *p,unsigned *flags){
  if(backend){
    xe_pipeline_draw d=xe_pipeline_for_draw(&static_cast<NativeVK::Pipeline*>(p)->publication);
    *flags=(d.placeholder?1u:0u)|(d.interpreter?2u:0u);
    VkPipeline native_handle=(VkPipeline)d.handle;
    return (uint64_t)native_handle;
  }
#ifndef XE_PIPELINE_VULKAN_ONLY
  bool placeholder,interpreter;
  auto* handle=xe::gpu::d3d12::PipelineCache::GetD3D12PipelineForDraw(p,&placeholder,&interpreter);
  *flags=(placeholder?1u:0u)|(interpreter?2u:0u);
  return (uint64_t)(uintptr_t)handle;
#else
  *flags=0;return 0;
#endif
}
extern "C" int xe_pipeline_native_clone(unsigned backend,void *p){
  if(!backend) {
#ifndef XE_PIPELINE_VULKAN_ONLY
    return NativeD3D::noncopyable();
#else
    return 0;
#endif
  }
  auto* entry=static_cast<NativeVK::Pipeline*>(p);
  auto* original=entry->retirement;
  auto* sentinel=reinterpret_cast<xe_pipeline_retire_slot*>(uintptr_t(0x1230));
  entry->retirement=sentinel;
  NativeVK::Pipeline copy(*entry),moved(std::move(copy));
  const bool sole_owner=entry->retirement==sentinel && !copy.retirement && !moved.retirement;
  entry->retirement=original;
  return sole_owner && xe_pipeline_handle_load(&entry->publication)==xe_pipeline_handle_load(&moved.publication) &&
      xe_pipeline_placeholder_load(&entry->publication)==xe_pipeline_placeholder_load(&moved.publication) &&
      xe_pipeline_flags_load(&entry->publication)==xe_pipeline_flags_load(&moved.publication) &&
      retro_atomic_load_acquire_ptr(&entry->pipeline_layout)==retro_atomic_load_acquire_ptr(&moved.pipeline_layout);
}
extern "C" unsigned xe_pipeline_native_size(unsigned backend){
  if(backend) return static_cast<unsigned>(sizeof(NativeVK::Pipeline));
#ifndef XE_PIPELINE_VULKAN_ONLY
  return NativeD3D::size();
#else
  return 0;
#endif
}
