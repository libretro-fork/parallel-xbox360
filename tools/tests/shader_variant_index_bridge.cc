/* Typed production-owner and exception/allocation ABI only. Scenarios are C89. */
#include "xenia/gpu/shader.h"
#include <new>
extern "C" void* xe_variant_allocate(size_t);
extern "C" void xe_variant_free(void*);
extern "C" uint64_t xe_variant_factory(void*,uint64_t,void*);
extern "C" void xe_variant_retired(void*,uint64_t,uint64_t);
void* operator new(size_t bytes) {
  if (void* p = xe_variant_allocate(bytes ? bytes : 1)) return p;
  throw std::bad_alloc();
}
void* operator new[](size_t bytes) { return ::operator new(bytes); }
void operator delete(void* p) noexcept { xe_variant_free(p); }
void operator delete[](void* p) noexcept { xe_variant_free(p); }
void operator delete(void* p,size_t) noexcept { xe_variant_free(p); }
void operator delete[](void* p,size_t) noexcept { xe_variant_free(p); }
class VariantOwner final : public xe::gpu::Shader {
 public:
  void* context;
  explicit VariantOwner(void* ctx) : Shader(xe::gpu::xenos::ShaderType::kVertex,
      0,nullptr,0,std::endian::native),context(ctx) {}
  class Value final : public Translation {
   public:
    void* context;uint64_t serial;
    Value(VariantOwner& owner,uint64_t key,uint64_t number)
        : Translation(owner,key),context(owner.context),serial(number) {}
    ~Value() override { xe_variant_retired(context,modification(),serial); }
  };
 protected:
  Translation* CreateTranslationInstance(uint64_t key) override {
    const uint64_t serial=xe_variant_factory(context,key,this);
    if (serial==UINT64_MAX) throw std::bad_alloc();
    return serial ? new Value(*this,key,serial) : nullptr;
  }
};
extern "C" void* xe_variant_open(void* context) { return new VariantOwner(context); }
extern "C" void xe_variant_close(void* owner) { delete static_cast<VariantOwner*>(owner); }
extern "C" void* xe_variant_find(void* owner,uint64_t key) {
  return static_cast<VariantOwner*>(owner)->GetTranslation(key);
}
extern "C" int xe_variant_create(void* owner,uint64_t key,int* is_new,void** out) {
  bool created=is_new && *is_new;*out=nullptr;
  try {
    *out=static_cast<VariantOwner*>(owner)->GetOrCreateTranslation(key,is_new ? &created : nullptr);
    if (is_new) *is_new=created ? 1 : 0;
    return *out ? 1 : 0;
  } catch (const std::bad_alloc&) { return -1; }
}
extern "C" void xe_variant_remove(void* owner,uint64_t key) {
  static_cast<VariantOwner*>(owner)->DestroyTranslation(key);
}
extern "C" uint64_t xe_variant_serial(const void* value) {
  return static_cast<const VariantOwner::Value*>(value)->serial;
}
extern "C" void xe_variant_sizes(size_t* owner,size_t* translation) {
  *owner=sizeof(VariantOwner);*translation=sizeof(VariantOwner::Value);
}
extern "C" unsigned xe_variant_intrusive(void) {
#ifdef XE_SHADER_INTRUSIVE_VARIANT_INDEX
  return 1;
#else
  return 0;
#endif
}
