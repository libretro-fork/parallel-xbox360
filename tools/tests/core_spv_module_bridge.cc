/* Full production builder control: deterministic compute modules, no GLSL. */
#include "SpvBuilder.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
int main(int argc, char **argv) {
  if (argc != 3) return 1;
  const unsigned counts[] = {1, 4, 8, 33, 129};
  const unsigned variant = unsigned(std::strtoul(argv[2], nullptr, 10));
  if (variant >= 5) return 2;
  spv::SpvBuildLogger logger;
  spv::Builder builder(spv::Spv_1_3, 0, &logger);
#ifdef XE_SPV_BINARY_CHECK
  // Exercise OpSourceContinued and module-process instructions as well as IR.
  builder.setSource(spv::SourceLanguage::GLSL, 450);
  builder.setDebugMainSourceFile("binary-output-test");
  builder.setSourceText(std::string(300000, 'x'));
  builder.addModuleProcessed("binary-output-regression");
#endif
  builder.addCapability(spv::Capability::Shader);
  builder.setMemoryModel(spv::AddressingModel::Logical, spv::MemoryModel::GLSL450);
  auto main = builder.makeEntryPoint("main");
  builder.addEntryPoint(spv::ExecutionModel::GLCompute, main, "main");
  builder.addExecutionMode(main, spv::ExecutionMode::LocalSize, 8, 4, 1);
  auto scalar = builder.makeFloatType(32);
  auto array = builder.makeArrayType(scalar, builder.makeUintConstant(counts[variant]), 0);
  auto local = builder.createVariable(spv::NoPrecision, spv::StorageClass::Function, array);
  std::vector<spv::Id> values;
  for (unsigned i = 0; i < counts[variant]; ++i) {
    auto a = builder.makeFloatConstant(float(i) * 0.125f);
    auto b = builder.makeFloatConstant(0.5f);
    values.push_back(builder.createBinOp(spv::Op::OpFAdd, scalar, a, b));
  }
  builder.createStore(builder.createCompositeConstruct(array, values), local);
  builder.makeReturn(true);
  builder.leaveFunction();
  builder.postProcess(false);
  std::vector<unsigned> words;
  builder.dump(words);
#ifdef XE_SPV_BINARY_CHECK
  xe_spv_binary binary = {};
  for (unsigned repeat = 0; repeat < 8; ++repeat) {
    xe_spv_binary_reset(&binary);
    if (!builder.dump(binary) || binary.count != words.size() ||
        std::memcmp(binary.words, words.data(), words.size() * sizeof(unsigned))) return 5;
  }
  if (xe_spv_binary_reserve(&binary, SIZE_MAX) || !binary.failed ||
      builder.dump(binary) || binary.count) return 6;
  xe_spv_binary_reset(&binary);
  if (!builder.dump(binary) || binary.count != words.size()) return 7;
  xe_spv_binary_close(&binary);
#endif
  FILE *file = std::fopen(argv[1], "wb");
  if (!file) return 3;
  bool ok = std::fwrite(words.data(), sizeof(unsigned), words.size(), file) == words.size();
  if (std::fclose(file) != 0) ok = false;
  return ok ? 0 : 4;
}
