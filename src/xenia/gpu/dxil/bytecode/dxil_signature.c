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

#include "dxil_module.h"

static const struct dxil_mdnode *
fill_SV_param_nodes(struct dxil_module *mod, unsigned record_id,
                    const struct dxil_signature_record *rec,
                    const struct dxil_psv_signature_element *psv,
                    bool is_input) {

   const struct dxil_mdnode *SV_params_nodes[11];
   /* For this to always work we should use vectorize_io, but for FS out and VS in
    * this is not implemented globally */
   const struct dxil_mdnode *flattened_semantics[32];
   const struct dxil_mdnode *SV_metadata[6];
   unsigned num_metadata_nodes = 0;
   unsigned i;
   uint8_t usage_mask;
   uint8_t dynamic_index_mask;
   if (!rec->num_elements || rec->num_elements > ARRAY_SIZE(flattened_semantics))
      return NULL;

   for (i = 0; i < rec->num_elements; ++i)
      flattened_semantics[i] = dxil_get_metadata_int32(mod, rec->elements[i].semantic_index);

   SV_params_nodes[0] = dxil_get_metadata_int32(mod, (int)record_id); /* Unique element ID */
   SV_params_nodes[1] = dxil_get_metadata_string(mod, rec->name); /* Element name */
   SV_params_nodes[2] = dxil_get_metadata_int8(mod, rec->sig_comp_type); /* Element type */
   SV_params_nodes[3] = dxil_get_metadata_int8(mod, (int8_t)psv->semantic_kind); /* Effective system value */
   SV_params_nodes[4] = dxil_get_metadata_node(mod, flattened_semantics,
                                               rec->num_elements); /* Semantic index vector */
   SV_params_nodes[5] = dxil_get_metadata_int8(mod, psv->interpolation_mode); /* Interpolation mode */
   SV_params_nodes[6] = dxil_get_metadata_int32(mod, psv->rows); /* Number of rows */
   SV_params_nodes[7] = dxil_get_metadata_int8(mod, psv->cols_and_start & 0xf); /* Number of columns */
   SV_params_nodes[8] = dxil_get_metadata_int32(mod, rec->elements[0].reg); /* Element packing start row */
   SV_params_nodes[9] = dxil_get_metadata_int8(mod, (psv->cols_and_start >> 4) & 0x3); /* Element packing start column */

   if (rec->elements[0].stream != 0) {
      SV_metadata[num_metadata_nodes++] = dxil_get_metadata_int32(mod, DXIL_SIGNATURE_ELEMENT_OUTPUT_STREAM);
      SV_metadata[num_metadata_nodes++] = dxil_get_metadata_int32(mod, rec->elements[0].stream);
   }
   usage_mask = rec->elements[0].always_reads_mask;
   if (!is_input)
      usage_mask = (uint8_t)(0xf & ~rec->elements[0].never_writes_mask);
   if (usage_mask && mod->minor_validator >= 5) {
      usage_mask >>= (psv->cols_and_start >> 4) & 0x3;
      SV_metadata[num_metadata_nodes++] = dxil_get_metadata_int32(mod, DXIL_SIGNATURE_ELEMENT_USAGE_COMPONENT_MASK);
      SV_metadata[num_metadata_nodes++] = dxil_get_metadata_int8(mod, usage_mask);
   }

   dynamic_index_mask = psv->dynamic_mask_and_stream & 0xf;
   if (dynamic_index_mask) {
      SV_metadata[num_metadata_nodes++] = dxil_get_metadata_int32(mod, DXIL_SIGNATURE_ELEMENT_DYNAMIC_INDEX_COMPONENT_MASK);
      SV_metadata[num_metadata_nodes++] = dxil_get_metadata_int8(mod, dynamic_index_mask);
   }

   SV_params_nodes[10] = num_metadata_nodes ? dxil_get_metadata_node(mod, SV_metadata, num_metadata_nodes) : NULL;

   return dxil_get_metadata_node(mod, SV_params_nodes, ARRAY_SIZE(SV_params_nodes));
}

static const struct dxil_mdnode *
get_signature_metadata(struct dxil_module *mod,
                       const struct dxil_signature_record *recs,
                       const struct dxil_psv_signature_element *psvs,
                       unsigned num_elements,
                       bool is_input)
{
   const struct dxil_mdnode *nodes[DXIL_SHADER_MAX_IO_ROWS];
   unsigned i;
   if (num_elements == 0)
      return NULL;

   if (num_elements > ARRAY_SIZE(nodes))
      return NULL;
   for (i = 0; i < num_elements; ++i) {
      nodes[i] = fill_SV_param_nodes(mod, i, &recs[i], &psvs[i], is_input);
      if (!nodes[i]) return NULL;
   }

   return dxil_get_metadata_node(mod, nodes, num_elements);
}

const struct dxil_mdnode *
get_signatures(struct dxil_module *mod)
{
   const struct dxil_mdnode *input_signature = get_signature_metadata(mod, mod->inputs, mod->psv_inputs, mod->num_sig_inputs, true);
   const struct dxil_mdnode *output_signature = get_signature_metadata(mod, mod->outputs, mod->psv_outputs, mod->num_sig_outputs, false);
   const struct dxil_mdnode *patch_const_signature = get_signature_metadata(mod, mod->patch_consts, mod->psv_patch_consts, mod->num_sig_patch_consts,
      mod->shader_kind == DXIL_DOMAIN_SHADER);

   const struct dxil_mdnode *SV_nodes[3] = {
      input_signature,
      output_signature,
      patch_const_signature
   };
   if (output_signature || input_signature || patch_const_signature)
      return dxil_get_metadata_node(mod, SV_nodes, ARRAY_SIZE(SV_nodes));
   else
      return NULL;
}
