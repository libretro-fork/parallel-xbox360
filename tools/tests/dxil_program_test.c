/* Whole-program resource/storage fixtures. MSVC C89; BSD license. */
#include "guest_program.h"
#include "shader_module.h"
#include <dxil_module.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define PAIR(al,ah,bl,bh) (uint32_t)(al), (((uint32_t)(ah)&65535)|((uint32_t)(bl)<<16)), (((uint32_t)(bl)>>16)|((uint32_t)(bh)<<16))
#define ALU(dest,exporting,relative,op,a,b,temps,addressed) \
  ((50u<<26)|(15u<<16)|((exporting)?1u<<15:0)|((relative)?1u<<6:0)|(dest)), \
  (uint32_t)(addressed), ((uint32_t)(temps)|((uint32_t)(op)<<24)|((uint32_t)(a)<<16)|((uint32_t)(b)<<8))
static const uint32_t sparse[] = {
  PAIR(0x3001,0x2000,0,0),
  ALU(0,1,0,2,65,65,0,0), ALU(1,1,0,0,0,255,0,0), ALU(2,1,0,2,1,1,0xc0000000,0)
};
static const uint32_t relative[] = {
  PAIR(2,0x7000,0x4002,0x1000), PAIR(1,0x8000,0,0x2000),
  ALU(0,1,0,2,5,5,0,0xe0000000), ALU(1,1,0,2,64,64,0xc0000000,0),
  ALU(0,0,1,2,10,10,0,0), ALU(2,1,0,2,64,64,0xc0000000,0)
};
static const uint32_t loops[] = {
  PAIR(3,0xc400,5,0x7000), PAIR(0x1006,0x1000,0x2008,0x9000),
  PAIR(2,0x8000,7,0xb400), PAIR(0x1007,0x2000,0x1008,0x2000),
  PAIR(0x1009,0x1000,0,0xa000), PAIR(0,0,0,0),
  0x04010000,0x80000000,0x80000000, 0xc8010000,0,0xa000ff00,
  0xc8018000,0,0xe2000000, 0xc8010001,0x00000100,0xa0010000
};
static const uint32_t vertex[] = {
  PAIR(0x52001,0x2000,0,0),
  (2u<<12)|(1u<<19)|(31u<<20)|(2u<<25),(33u<<16)|(1u<<13),2|(1u<<8),
  (3u<<12),(33u<<16)|(1u<<13)|(1u<<30),2u<<8
};
static const uint32_t texture[] = {
  PAIR(0xd54001,0x2000,0,0),
  24|(1u<<5)|(2u<<26),0,0,
  25|(1u<<5)|(36u<<26),0,0,
  26|(1u<<5)|(63u<<26),0,0,
  1|(1u<<5)|(2u<<12)|(4u<<26),1672|(1u<<29),1u<<14
};
static const uint32_t predicated[] = {
  PAIR(0x1001,0x2000,0,0),
  (50u<<26)|(1u<<16)|(1u<<15)|63, (1u<<28)|(1u<<27), (2u<<24)|(1u<<16)|(1u<<8)
};
typedef struct Fixture { struct dxil_module *module; XeDxilValue writes; } Fixture;
static int file_write(const char *path, const char *suffix, const void *data, size_t size) {
  size_t n = strlen(path), k = strlen(suffix)+1;
  char *name = (char *)malloc(n+k);
  FILE *file;
  int result;
  if (!name) return 0;
  memcpy(name,path,n); memcpy(name+n,suffix,k); file = fopen(name,"wb"); free(name);
  if (!file) return 0;
  result = fwrite(data,1,size,file) == size; if (fclose(file)) result = 0;
  return result;
}
static uint32_t bits(float v) { uint32_t u; memcpy(&u,&v,4); return u; }
static int export_written(void *context, XeDxilGuestProgram *program, unsigned index, unsigned mask) {
  Fixture *f = (Fixture *)context;
  XeDxilValue value;
  (void)program; (void)index; (void)mask;
  value = dxil_emit_load(f->module, f->writes, 4, 0);
  value = value ? dxil_emit_binop(f->module, DXIL_BINOP_ADD, value, dxil_module_get_int32_const(f->module,1),0) : NULL;
  return value && dxil_emit_store(f->module,value,f->writes,4,0);
}
static int texture_policy(void *context, XeDxilAlu *alu, XeDxilAluIo *io,
    const XeDxilTextureInstruction *instruction, const XeDxilTextureOperands *operands,
    unsigned serialized, XeDxilValue result[4]) {
  (void)context; (void)io;
  if (instruction->opcode != 1 || operands->coordinate_count != 2) return 0;
  result[0] = dxil_emit_binop(alu->module,DXIL_BINOP_ADD,operands->coordinates[0],operands->lod,0);
  result[1] = dxil_emit_binop(alu->module,DXIL_BINOP_ADD,operands->coordinates[1],operands->ddx[0],0);
  result[2] = dxil_emit_binop(alu->module,DXIL_BINOP_ADD,operands->ddx[2],operands->ddy[1],0);
  result[3] = dxil_module_get_float_const(alu->module,serialized ? 1.0f : 0.0f);
  return result[0] && result[1] && result[2] && result[3];
}
int main(int argc, char **argv) {
  static const uint64_t bitmap[4] = {1,2,0,((uint64_t)1<<63)};
  static const float sparse_data[12] = {1.25f,2.5f,3.75f,4,10,20,30,40,-1,-2,-3,-4};
  static const float sparse_expected[12] = {10,20,30,40,.25f,.5f,.75f,0,7,8,9,10};
  XeDxilShaderModule *shader;
  XeDxilShaderBinary binary;
  XeDxilGuestProgramOptions options;
  XeDxilGuestProgram *program = NULL;
  struct dxil_module *m;
  XeDxilAlu alu, *guest;
  XeDxilAluIo *io;
  XeDxilValue destination, values[4], value;
  uint32_t constants[1280], memory[32], golden[32];
  const uint32_t *words;
  size_t word_count;
  unsigned mode, i, j, cf_count, writes = 3;
  Fixture fixture;
  int result = 1;
  if (argc != 3) return 2;
  mode = (unsigned)strtoul(argv[2],NULL,10); if (mode > 8) return 2;
  memset(constants,0,sizeof(constants)); memset(golden,0,sizeof(golden));
  for (i = 0; i < 1024; ++i) constants[i] = bits((float)i);
  if (!mode) memcpy(constants,sparse_data,sizeof(sparse_data));
  constants[1024] = mode == 8 ? 3 : 1; constants[1024+8] = mode == 1 ? 1 : mode == 2 ? 0xff01 : 0x010203;
  for (i = 0; i < 192; ++i) constants[1088+i] = i*3+1;
  constants[1088+190] = 0x80000010;
  constants[1088+191] = mode == 6 ? 0 : 48;
  constants[1088+188] = mode == 1 ? (uint32_t)-1000000 : 1000000;
  for (i = 0; i < 32; ++i) memory[i] = i;
  if (!file_write(argv[1],".constants",constants,sizeof(constants)) || !file_write(argv[1],".memory",memory,sizeof(memory))) return 1;
  memset(&options,0,sizeof(options)); memset(&binary,0,sizeof(binary));
  shader = xe_dxil_shader_create(DXIL_COMPUTE_SHADER); if (!shader) return 1;
  m = xe_dxil_shader_module(shader); xe_dxil_alu_init(&alu,m); fixture.module = m;
  if (xe_dxil_shader_resource(shader,XE_DXIL_RESOURCE_UAV,DXIL_RESOURCE_KIND_RAW_BUFFER,0,0,1,0) != 0 ||
      xe_dxil_shader_resource(shader,XE_DXIL_RESOURCE_SRV,DXIL_RESOURCE_KIND_RAW_BUFFER,0,0,1,0) != 0 ||
      xe_dxil_shader_resource(shader,XE_DXIL_RESOURCE_CBV,DXIL_RESOURCE_KIND_CBUFFER,1,1,1,4096) != 0 ||
      xe_dxil_shader_resource(shader,XE_DXIL_RESOURCE_CBV,DXIL_RESOURCE_KIND_CBUFFER,1,3,1,160) != 1 ||
      xe_dxil_shader_resource(shader,XE_DXIL_RESOURCE_CBV,DXIL_RESOURCE_KIND_CBUFFER,1,4,1,768) != 2) goto done;
  destination = xe_dxil_resource_handle(&alu,XE_DXIL_RESOURCE_UAV,0,0);
  options.float_constants = xe_dxil_resource_handle(&alu,XE_DXIL_RESOURCE_CBV,0,1);
  options.bool_loop_constants = xe_dxil_resource_handle(&alu,XE_DXIL_RESOURCE_CBV,1,3);
  options.fetch_constants = xe_dxil_resource_handle(&alu,XE_DXIL_RESOURCE_CBV,2,4);
  options.shared_memory = xe_dxil_resource_handle(&alu,XE_DXIL_RESOURCE_SRV,0,0);
  options.register_count = mode == 3 || mode >= 4 ? 8 : 2;
  options.float_constant_count = mode ? 256 : 3;
  options.float_dynamic_addressing = mode >= 1 && mode <= 3;
  options.float_bitmap = !mode ? bitmap : NULL;
  options.context = &fixture; options.export_written = export_written; options.texture = texture_policy;
  fixture.writes = dxil_emit_alloca(m,dxil_module_get_int_type(m,32),dxil_module_get_int32_const(m,1),4);
  if (!fixture.writes || !dxil_emit_store(m,dxil_module_get_int32_const(m,0),fixture.writes,4,0)) goto done;
  program = xe_dxil_guest_program_create(m,&options); if (!program) goto done;
  guest = xe_dxil_guest_program_alu(program); io = xe_dxil_guest_program_io(program);
  for (i = 0; i < 4; ++i) values[i] = dxil_module_get_float_const(m,(float)(i+7));
  if (!xe_dxil_guest_program_register(program,1,15,values)) goto done;
  words = sparse; word_count = sizeof(sparse)/4; cf_count = 1;
  if (!mode) for (i = 0; i < 12; ++i) golden[(i/4)*8+i%4] = bits(sparse_expected[i]);
  if (mode == 1 || mode == 2) {
    words = relative; word_count = sizeof(relative)/4; cf_count = 4;
    guest->address = xe_dxil_cbuffer_word(&alu,options.fetch_constants,dxil_module_get_int32_const(m,188));
    for (i = 0; i < 4; ++i) {
      values[i] = dxil_module_get_float_const(m,(float)(i+2));
      golden[i] = bits((float)((mode == 1 ? 0 : 1020)+i));
      golden[8+i] = bits(mode == 1 ? (float)(i+2) : (float)(i+7));
      golden[16+i] = bits((float)(40+i));
    }
    if (!xe_dxil_guest_program_register(program,0,15,values)) goto done;
  } else if (mode == 3) {
    words = loops; word_count = sizeof(loops)/4; cf_count = 12; writes = 1;
    for (i = 0; i < 4; ++i) values[i] = dxil_module_get_float_const(m,0);
    if (!xe_dxil_guest_program_register(program,1,15,values)) goto done;
    golden[0] = bits(36); golden[8] = bits(3);
  } else if (mode == 4 || mode == 6) {
    words = vertex; word_count = sizeof(vertex)/4; writes = 0;
    for (i = 0; i < 4; ++i) { values[i] = dxil_module_get_float_const(m,1.75f); golden[i] = bits(mode == 4 ? 7.0f : 0.0f); golden[8+i] = bits(mode == 4 ? 8.0f : 0.0f); }
    if (!xe_dxil_guest_program_register(program,0,15,values)) goto done;
  } else if (mode == 5) {
    words = texture; word_count = sizeof(texture)/4; writes = 0;
    for (i = 0; i < 4; ++i) values[i] = dxil_module_get_float_const(m,(float)(2*(i+1)));
    if (!xe_dxil_guest_program_register(program,1,15,values)) goto done;
    golden[0] = bits(8); golden[1] = bits(6); golden[2] = bits(14); golden[3] = bits(1);
    for (i = 0; i < 4; ++i) golden[8+i] = bits((float)(2*(i+1)));
  } else if (mode >= 7) {
    words = predicated; word_count = sizeof(predicated)/4; writes = mode == 8;
    value = xe_dxil_cbuffer_word(&alu,options.bool_loop_constants,dxil_module_get_int32_const(m,0));
    value = value ? dxil_emit_binop(m,DXIL_BINOP_AND,value,dxil_module_get_int32_const(m,2),0) : NULL;
    guest->predicate = value ? dxil_emit_cmp(m,DXIL_ICMP_NE,value,dxil_module_get_int32_const(m,0)) : NULL;
    golden[24] = bits(mode == 8 ? 4.0f : 0.0f);
  }
  if (!xe_dxil_guest_program_emit(program,words,word_count,cf_count)) goto done;
  for (i = 0; i < 4; ++i) {
    for (j = 0; j < 4; ++j) {
      if (mode == 3 && i == 1) value = io->read(io->context,0,1,j,NULL);
      else if ((mode == 4 || mode == 6) && i < 2) value = io->read(io->context,0,2+i,j,NULL);
      else if (mode == 5 && i < 2) value = io->read(io->context,0,i ? 1 : 2,j,NULL);
      else value = xe_dxil_guest_program_export(program,i == 3 ? 63 : i,j);
      value = value ? dxil_emit_cast(m,DXIL_CAST_BITCAST,dxil_module_get_int_type(m,32),value) : NULL;
      if (!xe_dxil_raw_buffer_store(&alu,destination,dxil_module_get_int32_const(m,(int32_t)((i*8+j)*4)),value)) goto done;
    }
    for (j = 4; j < 8; ++j) {
      value = j == 6 ? dxil_emit_load(m,fixture.writes,4,0) : dxil_module_get_int32_const(m,0);
      golden[i*8+j] = j == 6 ? writes : 0;
      if (!xe_dxil_raw_buffer_store(&alu,destination,dxil_module_get_int32_const(m,(int32_t)((i*8+j)*4)),value)) goto done;
    }
  }
  if (!guest->failed && !alu.failed && dxil_emit_ret_void(m) && xe_dxil_shader_finish(shader,&binary) &&
      file_write(argv[1],"",binary.data,binary.size) && file_write(argv[1],".expected",golden,sizeof(golden))) result = 0;
done:
  xe_dxil_guest_program_destroy(program); xe_dxil_shader_binary_free(&binary); xe_dxil_shader_destroy(shader); return result;
}
