/* Typed access to the production sampling emitter; fixtures/checks are C89. */
#include "xenia/gpu/spirv_shader_translator.h"
#include "xenia/gpu/spirv_compatibility.h"
namespace xe {
namespace gpu {
struct SpirvTextureSamplingTestAccess {
  static int Emit(unsigned which, xe_spv_binary* output) {
    SpirvShaderTranslator::Features features(false);
    SpirvShaderTranslator translator(features,false,false,false,false,1,1);
    translator.builder_=std::make_unique<SpirvBuilder>(spv::Spv_1_3,0,nullptr);
    auto& b=*translator.builder_;
    b.addCapability(spv::CapabilityShader);
    b.setMemoryModel(spv::AddressingModelLogical,spv::MemoryModelGLSL450);
    translator.type_bool_=b.makeBoolType();
    translator.type_uint_=b.makeUintType(32);
    translator.type_float_=b.makeFloatType(32);
    translator.type_float4_=b.makeVectorType(translator.type_float_,4);
    translator.const_float4_0_=b.makeCompositeConstantRepeat(
        translator.type_float4_,b.makeFloatConstant(0),4);
    spv::Id image_type=b.makeImageType(translator.type_float_,
        (which&2)?spv::Dim3D:spv::Dim2D,false,false,false,1,
        spv::ImageFormatUnknown);
    spv::Id images[2];
    for(unsigned i=0;i<2;++i) {
      images[i]=b.createVariable(spv::NoPrecision,
          spv::StorageClassUniformConstant,image_type);
      b.addDecoration(images[i],spv::DecorationDescriptorSet,0);
      b.addDecoration(images[i],spv::DecorationBinding,int(i));
    }
    spv::Id sampler=b.createVariable(spv::NoPrecision,
        spv::StorageClassUniformConstant,b.makeSamplerType());
    b.addDecoration(sampler,spv::DecorationDescriptorSet,0);
    b.addDecoration(sampler,spv::DecorationBinding,2);
    spv::Id member_types[2]={translator.type_float4_,translator.type_float4_};
    spv::Id output_type=b.makeStructTypeView({member_types,2},"Output");
    b.addDecoration(output_type,spv::DecorationBlock);
    b.addMemberDecoration(output_type,0,spv::DecorationOffset,0);
    b.addMemberDecoration(output_type,1,spv::DecorationOffset,16);
    spv::Id destination=b.createVariable(spv::NoPrecision,
        spv::StorageClassStorageBuffer,output_type);
    b.addDecoration(destination,spv::DecorationDescriptorSet,0);
    b.addDecoration(destination,spv::DecorationBinding,3);
    member_types[0]=member_types[1]=translator.type_uint_;
    spv::Id push_type=b.makeStructTypeView({member_types,2},"Conditions");
    b.addDecoration(push_type,spv::DecorationBlock);
    b.addMemberDecoration(push_type,0,spv::DecorationOffset,0);
    b.addMemberDecoration(push_type,1,spv::DecorationOffset,4);
    spv::Id push=b.createVariable(spv::NoPrecision,
        spv::StorageClassPushConstant,push_type);
    auto* function=b.makeEntryPoint("main");
    b.addEntryPoint(spv::ExecutionModelGLCompute,function,"main");
    b.addExecutionMode(function,spv::ExecutionModeLocalSize,1,1,1);
    spv::Id conditions[2];
    for(unsigned i=0;i<2;++i) {
      spv::Id index=b.makeIntConstant(int(i));
      conditions[i]=b.createBinOp(spv::OpINotEqual,translator.type_bool_,
          b.createLoad(b.createAccessChainView(spv::StorageClassPushConstant,
              push,{&index,1}),spv::NoPrecision),b.makeUintConstant(0));
      images[i]=b.createLoad(images[i],spv::NoPrecision);
    }
    spv::Builder::TextureParameters params={};
    params.coords=b.smearScalar(spv::NoPrecision,b.makeFloatConstant(.5f),
        b.makeVectorType(translator.type_float_,(which&2)?3:2));
    params.lod=b.makeFloatConstant(0);
    spv::Id unsigned_result=b.smearScalar(spv::NoPrecision,
        b.makeFloatConstant(.75f),translator.type_float4_);
    spv::Id signed_result=b.smearScalar(spv::NoPrecision,
        b.makeFloatConstant(-.25f),translator.type_float4_);
    /* Outputs deliberately alias their own first interpolation endpoint. */
    translator.SampleTexture(params,spv::ImageOperandsLodMask,images[0],
        images[1],b.createLoad(sampler,spv::NoPrecision),conditions[0],
        conditions[1],(which&1)!=0,unsigned_result,signed_result,
        (which&4)?b.makeFloatConstant(.25f):spv::NoResult,
        unsigned_result,signed_result);
    spv::Id values[2]={unsigned_result,signed_result};
    for(unsigned i=0;i<2;++i) {
      spv::Id index=b.makeIntConstant(int(i));
      b.createStore(values[i],b.createAccessChainView(
          spv::StorageClassStorageBuffer,destination,{&index,1}));
    }
    b.makeReturn(true);b.leaveFunction();
    return b.dump(*output)?1:0;
  }
  static int EmitScale(unsigned mask,unsigned point,xe_spv_binary* output) {
    SpirvShaderTranslator::Features features(false);
    SpirvShaderTranslator translator(features,false,false,false,false,1,1);
    translator.builder_=std::make_unique<SpirvBuilder>(spv::Spv_1_3,0,nullptr);
    auto& b=*translator.builder_;
    b.addCapability(spv::CapabilityShader);
    b.setMemoryModel(spv::AddressingModelLogical,spv::MemoryModelGLSL450);
    translator.ext_inst_glsl_std_450_=b.import("GLSL.std.450");
    for(unsigned i=0;i<4;++i) {
      translator.type_bool_vectors_[i]=i?b.makeVectorType(translator.type_bool_,i+1):b.makeBoolType();
      translator.type_uint_vectors_[i]=i?b.makeVectorType(translator.type_uint_,i+1):b.makeUintType(32);
      translator.type_float_vectors_[i]=i?b.makeVectorType(translator.type_float_,i+1):b.makeFloatType(32);
      translator.const_float_vectors_0_[i]=i?b.makeCompositeConstantRepeat(translator.type_float_vectors_[i],translator.const_float_0_,i+1):b.makeFloatConstant(0);
      translator.const_float_vectors_1_[i]=i?b.makeCompositeConstantRepeat(translator.type_float_vectors_[i],translator.const_float_1_,i+1):b.makeFloatConstant(1);
    }
    translator.const_uint_0_=b.makeUintConstant(0);
    translator.const_uint4_0_=b.makeCompositeConstantRepeat(translator.type_uint4_,translator.const_uint_0_,4);
    spv::Id member=translator.type_float4_;
    spv::Id output_type=b.makeStructTypeView({&member,1},"Output");
    b.addDecoration(output_type,spv::DecorationBlock);
    b.addMemberDecoration(output_type,0,spv::DecorationOffset,0);
    spv::Id destination=b.createVariable(spv::NoPrecision,spv::StorageClassStorageBuffer,output_type);
    b.addDecoration(destination,spv::DecorationDescriptorSet,0);
    b.addDecoration(destination,spv::DecorationBinding,0);
    spv::Id members[5];for(unsigned i=0;i<5;++i)members[i]=translator.type_uint_;
    spv::Id push_type=b.makeStructTypeView({members,5},"Parameters");
    b.addDecoration(push_type,spv::DecorationBlock);
    for(unsigned i=0;i<5;++i)b.addMemberDecoration(push_type,i,spv::DecorationOffset,int(i*4));
    spv::Id push=b.createVariable(spv::NoPrecision,spv::StorageClassPushConstant,push_type);
    auto* function=b.makeEntryPoint("main");
    b.addEntryPoint(spv::ExecutionModelGLCompute,function,"main");
    b.addExecutionMode(function,spv::ExecutionModeLocalSize,1,1,1);
    spv::Id input[5];
    for(unsigned i=0;i<5;++i) {
      spv::Id index=b.makeIntConstant(int(i));
      input[i]=b.createLoad(b.createAccessChainView(spv::StorageClassPushConstant,push,{&index,1}),spv::NoPrecision);
    }
    spv::Id result[4];
    for(unsigned i=0;i<4;++i)result[i]=b.createUnaryOp(spv::OpBitcast,translator.type_float_,input[i+1]);
    translator.ApplyTextureIntegerScale(result,input[0],mask,point!=0);
    spv::Id index=b.makeIntConstant(0);
    b.createStore(b.createCompositeConstructView(translator.type_float4_,{result,4}),
        b.createAccessChainView(spv::StorageClassStorageBuffer,destination,{&index,1}));
    b.makeReturn(true);b.leaveFunction();
    return b.dump(*output)?1:0;
  }

};
}  // namespace gpu
}  // namespace xe
extern "C" int xe_texture_sampling_fixture(unsigned which,xe_spv_binary* output) {
  if(which>=8)return 0;
  return xe::gpu::SpirvTextureSamplingTestAccess::Emit(which,output);
}

extern "C" int xe_texture_scale_fixture(unsigned mask,unsigned point,xe_spv_binary* output) {
  if(!mask || mask>15 || point>1)return 0;
  return xe::gpu::SpirvTextureSamplingTestAccess::EmitScale(mask,point,output);
}
