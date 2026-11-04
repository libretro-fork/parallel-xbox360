#include "xenia/gpu/spirv/spvIR.h"
#include <cstring>
#include <cstdlib>
#define CHECK(x) do { if (!(x)) std::abort(); } while (0)
int main() {
  std::vector<unsigned> out;
  spv::Instruction inst(9, 3, spv::Op::OpIAdd);
  inst.addIdOperand(1); inst.addImmediateOperand(UINT32_MAX);
  inst.setIdOperand(0, 4); inst.setImmediateOperand(1, 7);
  CHECK(inst.getNumOperands() == 2 && inst.isIdOperand(0) && !inst.isIdOperand(1));
  inst.dump(out);
  const unsigned expected[] = { (5u << 16) | 128u, 3, 9, 4, 7 };
  CHECK(out.size() == 5 && !std::memcmp(out.data(), expected, sizeof(expected)));
  spv::Instruction name(spv::Op::OpName);
  name.addIdOperand(9); name.addStringOperand("test");
  CHECK(!std::strcmp(name.getNameString(), "test"));
  out.clear(); name.dump(out);
  const unsigned expected_name[] = { (4u << 16) | 5u, 9, 0x74736574u, 0 };
  CHECK(out.size() == 4 && !std::memcmp(out.data(), expected_name, sizeof(expected_name)));
  spv::Instruction text(spv::Op::OpString);
  text.addStringOperand("long shader source text crossing inline storage");
  CHECK(!std::strcmp(text.getNameString(), "long shader source text crossing inline storage"));
  for (unsigned i = 0; i < 4097; ++i) {
    if (i & 1) inst.addIdOperand(i); else inst.addImmediateOperand(i);
  }
  for (int i = 2; i < inst.getNumOperands(); ++i) CHECK(inst.isIdOperand(i) == bool((i - 2) & 1));
  bool failed = false;
  try { inst.reserveOperands(SIZE_MAX); } catch (const std::bad_alloc&) { failed = true; }
  CHECK(failed && inst.getNumOperands() == 4099);
  std::puts("PASS: production Instruction byte oracle, strings, setters, growth and failure");
}
