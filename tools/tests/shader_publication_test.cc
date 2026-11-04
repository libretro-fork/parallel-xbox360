/* Exercise the real translator's backend-preparation/publication boundary.
 * C++ bridge is required for the existing shader/translator virtual APIs. */
#include "xenia/gpu/shader_translator.h"
#include <retro_atomic.h>
#include <rthreads/rthreads.h>
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do { if (!(x)) std::abort(); } while (0)
static retro_atomic_int_t entered, release_hook, hook_calls;
static const uint32_t empty_ucode[3] = {0, 0, 0};
class TestTranslation : public xe::gpu::Shader::Translation {
 public:
  TestTranslation(xe::gpu::Shader& shader, bool fail)
      : Translation(shader, 0), fail_(fail) {}
  unsigned backend_ready = 0;
 protected:
  bool PrepareForPublication() override {
    CHECK(!is_translated() && translated_binary().size() == 4);
    retro_atomic_fetch_add_int(&hook_calls, 1);
    retro_atomic_store_release_int(&entered, 1);
    while (!retro_atomic_load_acquire_int(&release_hook)) {}
    backend_ready = 0x1234abcd;
    return !fail_;
  }
 private:
  bool fail_;
};
class TestShader : public xe::gpu::Shader {
 public:
  explicit TestShader(bool fail)
      : Shader(xe::gpu::xenos::ShaderType::kVertex, 0, empty_ucode, 3,
               std::endian::native), fail_(fail) {}
 protected:
  Translation* CreateTranslationInstance(uint64_t) override {
    return new TestTranslation(*this, fail_);
  }
 private:
  bool fail_;
};
class TestTranslator : public xe::gpu::ShaderTranslator {
 protected:
  std::vector<uint8_t> CompleteTranslation() override {
    return {0x12, 0x34, 0xab, 0xcd};
  }
};
static void translate(void* opaque) {
  auto* translation = static_cast<TestTranslation*>(opaque);
  if (translation->TryClaimTranslation()) {
    TestTranslator translator;
    translator.TranslateAnalyzedShader(*translation);
  } else {
    while (!translation->is_translated()) {}
  }
  CHECK(translation->backend_ready == 0x1234abcd);
  CHECK(translation->translated_binary()[3] == 0xcd);
}
int main() {
  for (unsigned fail = 0; fail < 2; ++fail) {
    TestShader shader(fail != 0);
    xe::StringBuffer disassembly;
    shader.AnalyzeUcode(disassembly);
    auto* translation = static_cast<TestTranslation*>(shader.GetOrCreateTranslation(0));
    sthread_t* workers[6];
    retro_atomic_store_relaxed_int(&entered, 0);
    retro_atomic_store_relaxed_int(&release_hook, 0);
    retro_atomic_store_relaxed_int(&hook_calls, 0);
    for (auto& worker : workers) {
      worker = sthread_create(translate, translation); CHECK(worker);
    }
    while (!retro_atomic_load_acquire_int(&entered)) {}
    CHECK(!translation->is_translated());
    retro_atomic_store_release_int(&release_hook, 1);
    for (auto* worker : workers) sthread_join(worker);
    CHECK(translation->is_translated() && translation->is_valid() == !fail);
    CHECK(retro_atomic_load_acquire_int(&hook_calls) == 1);
  }
  std::puts("PASS: six translation claimants, preparation before publication and backend failure visibility");
}
