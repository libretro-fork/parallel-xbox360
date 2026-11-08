/* Typed builder callbacks only; stack lowering lives in MSVC C89. */
#ifndef XENIA_GPU_SPIRV_SHADER_CALL_H_
#define XENIA_GPU_SPIRV_SHADER_CALL_H_
#include "libretro/core_shader_call.h"
#include "xenia/gpu/spirv_builder.h"
namespace xe::gpu {
struct SpirvCallAdapter {
  SpirvBuilder* builder;
  spv::Id scalar, vector, boolean;
  xe_shader_call_ops ops() {
    return {this,
      [](void* p,uint32_t value)->uint32_t {return self(p).builder->makeUintConstant(value);},
      [](void* p,uint32_t pointer)->uint32_t {return self(p).builder->createLoad(pointer,spv::NoPrecision);},
      [](void* p,uint32_t value,uint32_t pointer)->int {self(p).builder->createStore(value,pointer);return 1;},
      [](void* p,unsigned op,uint32_t a,uint32_t b)->uint32_t {
        auto& s=self(p);
        static constexpr spv::Op codes[]={spv::Op::OpIAdd,spv::Op::OpISub,spv::Op::OpULessThan,spv::Op::OpINotEqual};
        return s.builder->createBinOp(codes[op],op>=XE_CALL_LESS?s.boolean:s.scalar,a,b);
      },
      [](void* p,uint32_t value,unsigned index)->uint32_t {
        auto& s=self(p);return s.builder->createCompositeExtract(value,s.scalar,index);
      },
      [](void* p,const uint32_t* values)->uint32_t {
        auto& s=self(p);return s.builder->createCompositeConstructView(s.vector,xe_spv_words_view(values,4));
      }};
  }
 private:
  static SpirvCallAdapter& self(void* p) {return *static_cast<SpirvCallAdapter*>(p);}
};
}
#endif
