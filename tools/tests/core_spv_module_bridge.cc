/* Full production builder control: deterministic compute modules, no GLSL. */
#include "SpvBuilder.h"
#include <cstdio>
#include <cstdlib>
int main(int argc, char **argv) {
  if (argc != 3) return 1;
  const unsigned counts[] = {1, 4, 8, 33, 129};
  const unsigned variant = unsigned(std::strtoul(argv[2], nullptr, 10));
  if (variant >= 5) return 2;
  spv::SpvBuildLogger logger;
  spv::Builder builder(spv::Spv_1_3, 0, &logger);
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
  FILE *file = std::fopen(argv[1], "wb");
  if (!file) return 3;
  bool ok = std::fwrite(words.data(), sizeof(unsigned), words.size(), file) == words.size();
  if (std::fclose(file) != 0) ok = false;
  return ok ? 0 : 4;
}
