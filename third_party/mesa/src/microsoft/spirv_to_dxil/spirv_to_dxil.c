/*
 * Copyright © Microsoft Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "dxil_spirv_nir.h"
#include "spirv_to_dxil.h"
#include "dxil_nir.h"
#include "nir_to_dxil.h"
#include "shader_enums.h"
#include "spirv/nir_spirv.h"
#include "util/blob.h"

#include "git_sha1.h"
#include "vulkan/vulkan.h"

static_assert((mesa_shader_stage)DXIL_SPIRV_SHADER_NONE == MESA_SHADER_NONE, "must match");
static_assert((mesa_shader_stage)DXIL_SPIRV_SHADER_VERTEX == MESA_SHADER_VERTEX, "must match");
static_assert((mesa_shader_stage)DXIL_SPIRV_SHADER_TESS_CTRL == MESA_SHADER_TESS_CTRL, "must match");
static_assert((mesa_shader_stage)DXIL_SPIRV_SHADER_TESS_EVAL == MESA_SHADER_TESS_EVAL, "must match");
static_assert((mesa_shader_stage)DXIL_SPIRV_SHADER_GEOMETRY == MESA_SHADER_GEOMETRY, "must match");
static_assert((mesa_shader_stage)DXIL_SPIRV_SHADER_FRAGMENT == MESA_SHADER_FRAGMENT, "must match");
static_assert((mesa_shader_stage)DXIL_SPIRV_SHADER_COMPUTE == MESA_SHADER_COMPUTE, "must match");
static_assert((mesa_shader_stage)DXIL_SPIRV_SHADER_KERNEL == MESA_SHADER_KERNEL, "must match");

// Lower textures and samplers to SM 6.6+ descriptor-heap indexing when the
// caller asked for it. Buffers (UBO/SSBO) are left as direct descriptors, and
// no binding remap is needed, so the identity (NULL) remap is used.
static void
maybe_lower_to_bindless(nir_shader *nir,
                        const struct dxil_spirv_runtime_conf *conf)
{
   if (!conf->lower_to_bindless)
      return;
   struct dxil_spirv_nir_lower_bindless_options bindless_options = {
      .num_descriptor_sets = conf->bindless_descriptor_set_count,
      .dynamic_buffer_binding = ~0u,
      .remap_binding = NULL,
      .callback_context = NULL,
      .samplers_and_textures_only = true,
   };
   dxil_spirv_nir_lower_bindless(nir, &bindless_options);
}

bool
spirv_to_dxil(const uint32_t *words, size_t word_count,
              struct dxil_spirv_specialization *specializations,
              unsigned int num_specializations, dxil_spirv_shader_stage stage,
              const char *entry_point_name,
              enum dxil_validator_version validator_version_max,
              const struct dxil_spirv_debug_options *dgb_opts,
              const struct dxil_spirv_runtime_conf *conf,
              const struct dxil_spirv_logger *logger,
              struct dxil_spirv_object *out_dxil)
{
   if (stage == DXIL_SPIRV_SHADER_NONE || stage == DXIL_SPIRV_SHADER_KERNEL)
      return false;

   glsl_type_singleton_init_or_ref();

   struct nir_to_dxil_options opts = {
      .environment = DXIL_ENVIRONMENT_VULKAN,
      .disable_math_refactoring = conf->disable_math_refactoring,
      .shader_model_max = conf->shader_model_max,
      .validator_version_max = validator_version_max,
      .input_clip_size = conf->input_clip_size,
   };

   const struct spirv_to_nir_options *spirv_opts = dxil_spirv_nir_get_spirv_options();
   nir_shader_compiler_options nir_options;
   const unsigned supported_bit_sizes = 16 | 32 | 64;
   dxil_get_nir_compiler_options(&nir_options, conf->shader_model_max, supported_bit_sizes, supported_bit_sizes);
   // We will manually handle base_vertex when vertex_id and instance_id have
   // have been already converted to zero-base.
   nir_options.lower_base_vertex = conf->first_vertex_and_base_instance_mode != DXIL_SPIRV_SYSVAL_TYPE_ZERO;

   struct nir_spirv_specialization *spec = NULL;
   if (specializations && num_specializations > 0) {
      spec = vtn_alloc_specialization(num_specializations);

      for (unsigned i = 0; i < num_specializations; i++) {
         vtn_add_specialization_entry(spec, i, specializations[i].id,
                                      sizeof(specializations[i].value),
                                      &specializations[i].value, false);
      }
   }
   nir_shader *nir = spirv_to_nir(words, word_count, spec,
                                  (mesa_shader_stage)stage, entry_point_name,
                                  spirv_opts, &nir_options);
   vtn_free_specialization(spec);
   if (!nir) {
      glsl_type_singleton_decref();
      return false;
   }

   nir_validate_shader(nir,
                       "Validate before feeding NIR to the DXIL compiler");

   dxil_spirv_nir_prep(nir);

   dxil_spirv_nir_passes(nir, conf, &out_dxil->metadata);

   maybe_lower_to_bindless(nir, conf);

   if (dgb_opts->dump_nir)
      nir_print_shader(nir, stderr);

   struct dxil_logger logger_inner = {.priv = logger->priv,
                                      .log = logger->log};

   struct blob dxil_blob;
   if (!nir_to_dxil(nir, &opts, &logger_inner, &dxil_blob)) {
      if (dxil_blob.allocated)
         blob_finish(&dxil_blob);
      ralloc_free(nir);
      glsl_type_singleton_decref();
      return false;
   }

   ralloc_free(nir);
   blob_finish_get_buffer(&dxil_blob, &out_dxil->binary.buffer,
                          &out_dxil->binary.size);

   glsl_type_singleton_decref();
   return true;
}

bool
spirv_to_dxil_link(const struct dxil_spirv_link_stage *stages, size_t num_stages,
                   enum dxil_validator_version validator_version_max,
                   const struct dxil_spirv_debug_options *dgb_opts,
                   const struct dxil_spirv_runtime_conf *conf,
                   const struct dxil_spirv_logger *logger,
                   struct dxil_spirv_object *out_dxil)
{
   enum { MAX_LINK_STAGES = 5 };
   if (num_stages == 0 || num_stages > MAX_LINK_STAGES)
      return false;

   memset(out_dxil, 0, num_stages * sizeof(*out_dxil));

   glsl_type_singleton_init_or_ref();

   struct nir_to_dxil_options opts = {
      .environment = DXIL_ENVIRONMENT_VULKAN,
      .disable_math_refactoring = conf->disable_math_refactoring,
      .shader_model_max = conf->shader_model_max,
      .validator_version_max = validator_version_max,
      .input_clip_size = conf->input_clip_size,
   };

   const struct spirv_to_nir_options *spirv_opts =
      dxil_spirv_nir_get_spirv_options();
   // One set of compiler options shared by every stage. It must outlive the
   // nir_shaders, which keep a pointer to it until nir_to_dxil runs.
   nir_shader_compiler_options nir_options;
   const unsigned supported_bit_sizes = 16 | 32 | 64;
   dxil_get_nir_compiler_options(&nir_options, conf->shader_model_max,
                                 supported_bit_sizes, supported_bit_sizes);
   nir_options.lower_base_vertex =
      conf->first_vertex_and_base_instance_mode != DXIL_SPIRV_SYSVAL_TYPE_ZERO;

   nir_shader *nirs[MAX_LINK_STAGES] = {0};
   struct dxil_spirv_metadata metadata[MAX_LINK_STAGES] = {0};
   bool ok = true;

   // spirv_to_nir, prep and the common passes for every stage.
   for (size_t i = 0; i < num_stages; i++) {
      nirs[i] = spirv_to_nir(
         stages[i].words, stages[i].word_count, NULL,
         (mesa_shader_stage)stages[i].stage,
         stages[i].entry_point_name ? stages[i].entry_point_name : "main",
         spirv_opts, &nir_options);
      if (!nirs[i]) {
         ok = false;
         break;
      }
      nir_validate_shader(nirs[i], "Validate before linking the DXIL stages");
      dxil_spirv_nir_prep(nirs[i]);
      dxil_spirv_nir_passes(nirs[i], conf, &metadata[i]);
   }

   // Link consecutive stages from last to first, so each stage tells its
   // predecessor which varyings it consumes. This reconciles the inter-stage
   // signatures the way D3D12 requires.
   if (ok) {
      for (size_t cur = num_stages; cur-- > 1;) {
         dxil_spirv_nir_link(nirs[cur], nirs[cur - 1], conf, &metadata[cur]);
      }
   }

   // Bindless lowering and DXIL emission per stage.
   for (size_t i = 0; i < num_stages && ok; i++) {
      maybe_lower_to_bindless(nirs[i], conf);

      if (dgb_opts->dump_nir)
         nir_print_shader(nirs[i], stderr);

      struct dxil_logger logger_inner = {.priv = logger->priv,
                                         .log = logger->log};
      struct blob dxil_blob;
      if (!nir_to_dxil(nirs[i], &opts, &logger_inner, &dxil_blob)) {
         if (dxil_blob.allocated)
            blob_finish(&dxil_blob);
         ok = false;
         break;
      }
      blob_finish_get_buffer(&dxil_blob, &out_dxil[i].binary.buffer,
                             &out_dxil[i].binary.size);
      out_dxil[i].metadata = metadata[i];
   }

   for (size_t i = 0; i < num_stages; i++) {
      if (nirs[i])
         ralloc_free(nirs[i]);
   }

   glsl_type_singleton_decref();

   if (!ok) {
      for (size_t i = 0; i < num_stages; i++) {
         if (out_dxil[i].binary.buffer) {
            free(out_dxil[i].binary.buffer);
            out_dxil[i].binary.buffer = NULL;
            out_dxil[i].binary.size = 0;
         }
      }
      return false;
   }
   return true;
}

void
spirv_to_dxil_free(struct dxil_spirv_object *dxil)
{
   free(dxil->binary.buffer);
}

uint64_t
spirv_to_dxil_get_version()
{
   const char sha1[] = MESA_GIT_SHA1;
   const char* dash = strchr(sha1, '-');
   if (dash) {
      return strtoull(dash + 1, NULL, 16);
   }
   return 0;
}
