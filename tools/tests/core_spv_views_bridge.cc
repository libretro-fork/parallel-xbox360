/* C++ interop control: archived vector builder versus borrowed C89 inputs.
 * Scratch is overwritten immediately after each call to expose retained views. */
#include "xenia/gpu/spirv_builder.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
using xe::gpu::SpirvBuilder;
using spv::Id;
#ifdef XE_SPV_OUTPUT_LEGACY
#define WORDS(p, n) std::vector<Id>((p), (p) + (n))
#else
#define WORDS(p, n) xe_spv_words_view((p), (n))
#endif
#define CHECK(x) do { if (!(x)) std::abort(); } while (0)
int main(int argc, char** argv) {
  if (argc != 3) return 1;
  unsigned variant = unsigned(std::strtoul(argv[2], nullptr, 10));
  if (variant >= 8) return 2;
  SpirvBuilder builder(spv::Spv_1_3, 0, nullptr);
  builder.addCapability(spv::Capability::Shader);
  builder.setMemoryModel(spv::AddressingModel::Logical, spv::MemoryModel::GLSL450);
  builder.setUseReplicatedComposites((variant & 4) != 0);
  spv::Function* main = builder.makeEntryPoint("main");
  builder.addEntryPoint(spv::ExecutionModel::GLCompute, main, "main");
  builder.addExecutionMode(main, spv::ExecutionMode::LocalSize, 1, 1, 1);
  Id uint_type = builder.makeUintType(32);
  Id float_type = builder.makeFloatType(32);
  Id uint2 = builder.makeVectorType(uint_type, 2);
  Id uint4 = builder.makeVectorType(uint_type, 4);
  Id zero = builder.makeUintConstant(0);
  Id one = builder.makeUintConstant(1);
  Id two = builder.makeUintConstant(2);
  // Parameter records are owned by Function, rather than its block vectors.
  // Exercise their construction, calls and teardown in both archived/current IR.
  spv::Block* main_block = builder.getBuildPoint();
  spv::Block* callee_entry;
  std::vector<Id> parameters(9, uint_type);
  auto callee = builder.makeFunctionEntry(spv::NoPrecision, uint_type, "callee",
      parameters, {}, &callee_entry);
  builder.makeReturn(true, callee->getParamId(0));
  builder.leaveFunction();
  builder.setBuildPoint(main_block);
  std::vector<Id> arguments(9, one);
  builder.createFunctionCall(callee, arguments);
  Id array = builder.makeArrayType(uint_type, builder.makeUintConstant(4), 0);
  Id members[3] = {uint_type, float_type, array};
#ifdef XE_SPV_OUTPUT_LEGACY
  Id structure = builder.makeStructType(WORDS(members, 3), "BorrowedStruct");
#else
  Id structure = builder.makeStructTypeView(WORDS(members, 3), "BorrowedStruct");
#endif
  members[0] = members[1] = members[2] = 0;
  CHECK(builder.getContainedTypeId(structure, 0) == uint_type);
  Id variable = builder.createVariable(spv::NoPrecision, spv::StorageClass::Function, structure);
  Id dynamic_index = builder.createBinOp(spv::Op::OpIAdd, uint_type, zero, one);
  Id indices[2] = {two, dynamic_index};
#ifdef XE_SPV_OUTPUT_LEGACY
  Id address = builder.createAccessChain(spv::StorageClass::Function, variable, WORDS(indices, 2));
#else
  // Direct base API must use its arguments and preserve unrelated frontend state.
  builder.clearAccessChain();
  builder.setAccessChainLValue(variable);
  builder.accessChainPush(one, {}, 0);
  Id address = builder.spv::Builder::createAccessChainView(
      spv::StorageClass::Function, variable, WORDS(indices, 2));
  CHECK(builder.getResultingAccessChainType() == float_type);
  builder.clearAccessChain();
#endif
  indices[0] = indices[1] = 0;
  CHECK(builder.getContainedTypeId(builder.getTypeId(address)) == uint_type);
  builder.createStore(one, address);
  Id loaded = builder.createLoad(address, spv::NoPrecision);
  Id args[2] = {loaded, two};
#ifdef XE_SPV_OUTPUT_LEGACY
  builder.createOp(spv::Op::OpIAdd, uint_type, WORDS(args, 2));
#else
  builder.createOpView(spv::Op::OpIAdd, uint_type, WORDS(args, 2));
#endif
  args[0] = args[1] = 0;
  Id scalar = builder.makeUintConstant(7, (variant & 1) != 0);
  Id values[4] = {scalar, scalar, scalar, scalar};
#ifdef XE_SPV_OUTPUT_LEGACY
  Id constant = builder.makeCompositeConstant(uint4, WORDS(values, 4), (variant & 1) != 0);
#else
  Id constant = builder.makeCompositeConstantView(uint4, WORDS(values, 4), (variant & 1) != 0);
#endif
  if (variant & 2) builder.setToSpecConstCodeGenMode();
#ifdef XE_SPV_OUTPUT_LEGACY
  builder.createCompositeConstruct(uint4, WORDS(values, 4));
#else
  builder.createCompositeConstructView(uint4, WORDS(values, 4));
#endif
  values[0] = values[1] = values[2] = values[3] = 0;
  uint32_t channels[2] = {3, 1};
#ifdef XE_SPV_OUTPUT_LEGACY
  builder.createRvalueSwizzle(spv::NoPrecision, uint2, constant, WORDS(channels, 2));
#else
  builder.createRvalueSwizzleView(spv::NoPrecision, uint2, constant, WORDS(channels, 2));
#endif
  channels[0] = channels[1] = 0;
  builder.setToNormalCodeGenMode();
  Id spec_args[2] = {one, two};
#ifdef XE_SPV_OUTPUT_LEGACY
  builder.createSpecConstantOp(spv::Op::OpIAdd, uint_type, WORDS(spec_args, 2), {});
#else
  builder.createSpecConstantOpView(spv::Op::OpIAdd, uint_type, WORDS(spec_args, 2), xe_spv_words_view(nullptr, 0));
#endif
  spec_args[0] = spec_args[1] = 0;
  Id barrier_args[2] = {
      builder.makeUintConstant(uint32_t(spv::Scope::Device)),
      builder.makeUintConstant(uint32_t(spv::MemorySemanticsMask::AcquireRelease) |
                               uint32_t(spv::MemorySemanticsMask::UniformMemory))};
#ifdef XE_SPV_OUTPUT_LEGACY
  builder.createNoResultOp(spv::Op::OpMemoryBarrier, WORDS(barrier_args, 2));
#else
  builder.createNoResultOpView(spv::Op::OpMemoryBarrier, WORDS(barrier_args, 2));
#endif
  barrier_args[0] = barrier_args[1] = 0;
  auto* loop_header = &builder.makeNewBlock();
  auto* loop_body = &builder.makeNewBlock();
  auto* loop_continue = &builder.makeNewBlock();
  auto* loop_merge = &builder.makeNewBlock();
  builder.createBranch(loop_header);
  builder.setBuildPoint(loop_header);
  uint32_t loop_args[1] = {3};
#ifdef XE_SPV_OUTPUT_LEGACY
  builder.createLoopMerge(loop_merge, loop_continue,
      spv::LoopControlMask::DependencyLength, WORDS(loop_args, 1));
#else
  builder.createLoopMergeView(loop_merge, loop_continue,
      spv::LoopControlMask::DependencyLength, WORDS(loop_args, 1));
#endif
  loop_args[0] = 0;
  builder.createConditionalBranch(builder.makeBoolConstant(false), loop_body, loop_merge);
  builder.setBuildPoint(loop_body);
  builder.createBranch(loop_continue);
  builder.setBuildPoint(loop_continue);
  builder.createBranch(loop_header);
  builder.setBuildPoint(loop_merge);
  builder.makeReturn(true);
  builder.leaveFunction();
  builder.postProcess(false);
  std::vector<unsigned> words;
  builder.dump(words);
  FILE* file = std::fopen(argv[1], "wb");
  if (!file) return 3;
  bool ok = fwrite(words.data(), sizeof(unsigned), words.size(), file) == words.size();
  if (fclose(file)) ok = false;
  return ok ? 0 : 4;
}
