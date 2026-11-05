/* Typed builder interop only: cross-block sampled-image rewrites must preserve
 * output against the archived vector owner. No frontend, renderer or GLSL. */
#include "SpvBuilder.h"
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
struct SamplerBuilder : spv::Builder {
  using spv::Builder::Builder;
  using spv::Builder::createBranch;
};
int main(int argc, char** argv) {
  if (argc != 2) return 1;
  SamplerBuilder builder(spv::Spv_1_3, 0, nullptr);
  builder.addCapability(spv::Capability::Shader);
  builder.setMemoryModel(spv::AddressingModel::Logical, spv::MemoryModel::GLSL450);
  auto* main = builder.makeEntryPoint("main");
  builder.addEntryPoint(spv::ExecutionModel::GLCompute, main, "main");
  builder.addExecutionMode(main, spv::ExecutionMode::LocalSize, 1, 1, 1);
  auto f32 = builder.makeFloatType(32);
  auto vec2 = builder.makeVectorType(f32, 2);
  auto vec4 = builder.makeVectorType(f32, 4);
  auto image = builder.makeImageType(f32, spv::Dim::Dim2D, false, false, false, 1, spv::ImageFormat::Unknown);
  auto sampler = builder.makeSamplerType();
  auto sampled = builder.makeSampledImageType(image);
  auto imageVar = builder.createVariable(spv::NoPrecision, spv::StorageClass::UniformConstant, image);
  auto samplerVar = builder.createVariable(spv::NoPrecision, spv::StorageClass::UniformConstant, sampler);
  builder.addDecoration(imageVar, spv::Decoration::DescriptorSet, 0);
  builder.addDecoration(imageVar, spv::Decoration::Binding, 0);
  builder.addDecoration(samplerVar, spv::Decoration::DescriptorSet, 0);
  builder.addDecoration(samplerVar, spv::Decoration::Binding, 1);
  auto imageValue = builder.createLoad(imageVar, spv::NoPrecision);
  auto samplerValue = builder.createLoad(samplerVar, spv::NoPrecision);
  auto combined = builder.createBinOp(spv::Op::OpSampledImage, sampled, imageValue, samplerValue);
  // Same-block use must not be cloned.
  builder.createUnaryOp(spv::Op::OpImage, image, combined);
  auto* consumer = &builder.makeNewBlock();
  builder.createBranch(false, consumer);
  builder.setBuildPoint(consumer);
  auto zero = builder.makeFloatConstant(0.0f);
  auto coords = builder.makeCompositeConstant(vec2, {zero, zero});
  // First body node, repeated consumers and ordinary nodes between them.
  auto first = builder.createOp(spv::Op::OpImageSampleExplicitLod, vec4,
      std::vector<spv::IdImmediate>{{true, combined}, {true, coords},
      {false, spv::ImageOperandsMask::Lod}, {true, zero}});
  auto second = builder.createUnaryOp(spv::Op::OpImage, image, combined);
  builder.makeReturn(true);
  builder.leaveFunction();
  builder.postProcess(false);
  CHECK(builder.getIdOperand(first, 0) != combined && builder.getIdOperand(second, 0) != combined);
  CHECK(builder.getIdOperand(first, 0) != builder.getIdOperand(second, 0));
  std::vector<unsigned> words;
  builder.dump(words);
  // Count clones in wire output independently of the owner representation.
  unsigned combinedCount = 0;
  for (size_t i = 5; i < words.size();) {
    unsigned count = words[i] >> spv::WordCountShift;
    CHECK(count && i + count <= words.size());
    if ((words[i] & 0xffff) == unsigned(spv::Op::OpSampledImage)) ++combinedCount;
    i += count;
  }
  CHECK(combinedCount == 3);
  FILE* file = std::fopen(argv[1], "wb");
  if (!file) return 2;
  bool ok = std::fwrite(words.data(), sizeof(unsigned), words.size(), file) == words.size();
  if (std::fclose(file)) ok = false;
  std::puts("PASS: cross-block sampled-image cloning, same-block identity and repeated consumers");
  return ok ? 0 : 3;
}
