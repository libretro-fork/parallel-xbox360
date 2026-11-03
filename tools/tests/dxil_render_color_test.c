/* Dense numeric EDRAM color fixtures. MSVC C89; BSD license. */
#include "shader_module.h"
#include "guest_resources.h"
#include "edram_format.h"
#include "dxil_render_color_reference.h"
#include <dxil_module.h>
#include <dxil_function.h>
#include <stdio.h>
#include <stdlib.h>
static XeDxilValue ic(struct dxil_module *m, uint32_t v) { return dxil_module_get_int32_const(m,(int32_t)v); }
static XeDxilValue add(struct dxil_module *m, XeDxilValue a, XeDxilValue b) { return dxil_emit_binop(m,DXIL_BINOP_ADD,a,b,0); }
static XeDxilValue mul(struct dxil_module *m, XeDxilValue a, XeDxilValue b) { return dxil_emit_binop(m,DXIL_BINOP_MUL,a,b,0); }
static int write_file(const char *path, const char *suffix, const void *data, size_t bytes) {
  char *name = (char *)malloc(strlen(path)+strlen(suffix)+1); FILE *file; int ok;
  if (!name) return 0;
  strcpy(name,path); strcat(name,suffix); file = fopen(name,"wb"); free(name);
  if (!file) return 0;
  ok = fwrite(data,1,bytes,file) == bytes; if (fclose(file)) ok = 0; return ok;
}
static int cases(const char *path, unsigned f) {
  static const uint32_t patterns[] = {0,0x80000000,0x7f800000,0xff800000,0x7fc12345,0x3f000000,0x3f800000,0xbf800000,0xc2000000,0x42000000,0x47ffe000,0xc7ffe000,0x47800000,0x477ff000,0x38800000,0x33000000,0x33800000,0x3e800000,0x3effffff,0x41ff0000,0x7fffffff,1,0x80000001,0x3f808000,0x3f801000,0x3f803000,0x3f805000,0xc0000000,0x40000000,0xc1c00000,0x41c00000,0x3b000000};
  unsigned count = f >= 4 && f <= 7 ? 65536 : 4096, i, j, n; uint32_t constants[4], *input, *expected, p[2]; float values[4], a, b; int ok;
  input = (uint32_t *)calloc((size_t)count*8,4); expected = (uint32_t *)calloc((size_t)count*8,4);
  if (!input || !expected) { free(input); free(expected); return 0; }
  for (i = 0; i < count; ++i) {
    for (j = 0; j < 4; ++j) { n = i*(j*2+1)+j*151; input[i*8+j] = patterns[(i+j*7)%32]; p[j/2] = j&1 ? p[j/2] | ((n&65535)<<16) : n&65535; }
    if (!(f >= 4 && f <= 7)) { p[0] = i*2654435761u; p[1] = patterns[i%32]; }
    if (i >= 32 && f >= 4 && f <= 7) for (j = 0; j < 4; ++j) {
      n = (i*(j*2+1)+j*151)&65535;
      if (f >= 6) {
        a = color_half_decode(n); b = color_half_decode((n&32767) == 32767 ? n : n+1);
        if (j) a = (a+b)*.5f;
        if (j == 2 && a != 0) a = color_float(color_bits(a)+1);
      } else a = (float)(n&32768 ? (int)n-65536 : (int)n)*(32.0f/32767);
      input[i*8+j] = color_bits(a);
    }
    input[i*8+4] = p[0]; input[i*8+5] = p[1];
    for (j = 0; j < 4; ++j) values[j] = color_float(input[i*8+j]);
    color_pack(f,values,expected+i*8); color_unpack(f,p,values);
    for (j = 0; j < 4; ++j) expected[i*8+2+j] = color_bits(values[j]);
    color_pack(f,values,expected+i*8+6);
  }
  memset(constants,0,sizeof(constants)); constants[0] = count;
  ok = write_file(path,".memory",input,(size_t)count*32) && write_file(path,".expected",expected,(size_t)count*32) && write_file(path,".constants",constants,sizeof(constants));
  free(input); free(expected); return ok;
}
int main(int argc, char **argv) {
  XeDxilShaderModule *shader; XeDxilShaderBinary binary; XeDxilAlu alu; struct dxil_module *m; const struct dxil_func *thread;
  XeDxilValue source, destination, constants, count, storage, index, address, words[8], values[4], packed[2], args[2], value;
  unsigned f, blocks, i; int result = 1;
  if (argc != 3) return 2;
  f = (unsigned)strtoul(argv[2],NULL,10);
  if (!(f < 8 || f == 10 || f == 12 || f == 14 || f == 15) || !cases(argv[1],f)) return 2;
  memset(&binary,0,sizeof(binary)); shader = xe_dxil_shader_create(DXIL_COMPUTE_SHADER); if (!shader) return 1;
  m = xe_dxil_shader_module(shader); xe_dxil_alu_init(&alu,m);
  if (!xe_dxil_shader_threads(shader,64,1,1) || xe_dxil_shader_resource(shader,XE_DXIL_RESOURCE_UAV,DXIL_RESOURCE_KIND_RAW_BUFFER,0,0,1,0) != 0 || xe_dxil_shader_resource(shader,XE_DXIL_RESOURCE_SRV,DXIL_RESOURCE_KIND_RAW_BUFFER,0,0,1,0) != 0 || xe_dxil_shader_resource(shader,XE_DXIL_RESOURCE_CBV,DXIL_RESOURCE_KIND_CBUFFER,1,4,1,16) != 0) goto done;
  source = xe_dxil_resource_handle(&alu,XE_DXIL_RESOURCE_SRV,0,0); destination = xe_dxil_resource_handle(&alu,XE_DXIL_RESOURCE_UAV,0,0); constants = xe_dxil_resource_handle(&alu,XE_DXIL_RESOURCE_CBV,0,4); count = xe_dxil_cbuffer_word(&alu,constants,ic(m,0));
  thread = dxil_get_function(m,"dx.op.threadId",DXIL_I32); args[0] = ic(m,93); args[1] = ic(m,0); value = thread ? dxil_emit_call(m,thread,args,2) : NULL;
  storage = dxil_emit_alloca(m,dxil_module_get_int_type(m,32),ic(m,1),4);
  if (!value || !storage || !count || !dxil_emit_store(m,value,storage,4,0) || !dxil_function_reserve_blocks(m,3,&blocks) || !dxil_emit_branch(m,NULL,blocks,0)) goto done;
  m->cur_emitting_func->basic_block_ids[blocks] = (int)m->cur_emitting_func->curr_block;
  index = dxil_emit_load(m,storage,4,0); value = dxil_emit_cmp(m,DXIL_ICMP_ULT,index,count);
  if (!value || !dxil_emit_branch(m,value,blocks+1,blocks+2)) goto done;
  m->cur_emitting_func->basic_block_ids[blocks+1] = (int)m->cur_emitting_func->curr_block;
  address = mul(m,index,ic(m,32));
  for (i = 0; i < 6; ++i) words[i] = xe_dxil_raw_buffer_load(&alu,source,add(m,address,ic(m,i*4)));
  for (i = 0; i < 4; ++i) values[i] = dxil_emit_cast(m,DXIL_CAST_BITCAST,dxil_module_get_float_type(m,32),words[i]);
  if (!xe_dxil_pack_render_color(&alu,f,values,packed)) goto done;
  words[0] = packed[0]; words[1] = packed[1]; packed[0] = words[4]; packed[1] = words[5];
  if (!xe_dxil_unpack_render_color(&alu,f,packed,values)) goto done;
  for (i = 0; i < 4; ++i) words[2+i] = dxil_emit_cast(m,DXIL_CAST_BITCAST,dxil_module_get_int_type(m,32),values[i]);
  if (!xe_dxil_pack_render_color(&alu,f,values,packed)) goto done;
  words[6] = packed[0]; words[7] = packed[1];
  for (i = 0; i < 8; ++i) if (!xe_dxil_raw_buffer_store(&alu,destination,add(m,address,ic(m,i*4)),words[i])) goto done;
  if (!dxil_emit_store(m,add(m,index,ic(m,64)),storage,4,0) || !dxil_emit_branch(m,NULL,blocks,0)) goto done;
  m->cur_emitting_func->basic_block_ids[blocks+2] = (int)m->cur_emitting_func->curr_block;
  if (!alu.failed && dxil_emit_ret_void(m) && xe_dxil_shader_finish(shader,&binary)) result = !write_file(argv[1],"",binary.data,binary.size);
 done: xe_dxil_shader_binary_free(&binary); xe_dxil_shader_destroy(shader); return result;
}
