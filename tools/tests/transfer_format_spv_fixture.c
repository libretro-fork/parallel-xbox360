/* Two-pass declarations/IDs, direct final SPIR-V; no temporary word stream. */
#include "libretro/core_transfer_format_spv.h"
#include "transfer_format_cases.h"
enum { FV=1,FU,FF,FB,FV3,FA,FS,FPB,FPU,FPI,FFT,FIN,FOUT,FINV,FMAIN,FENTRY,FEND,FEXT,FWORK,
       FCASE=64,FMERGE=92,FCONST=128,FDYNAMIC=1024 };
typedef struct format_fixture {
 xe_spv_binary *out;uint32_t next;
 struct {unsigned floating;uint32_t bits;} constants[256];
 uint32_t precise[1024];unsigned constant_count,precise_count,planning,failed;
} format_fixture;
static uint32_t format_constant(void *opaque,unsigned floating,uint32_t bits)
{
 format_fixture *f=(format_fixture*)opaque;unsigned i;
 for(i=0;i<f->constant_count;++i)if(f->constants[i].floating==floating && f->constants[i].bits==bits)return FCONST+i;
 if(!f->planning || i>=256){f->failed=1;return 0;}
 f->constants[i].floating=floating;f->constants[i].bits=bits;++f->constant_count;return FCONST+i;
}
static int format_precise(void *opaque,uint32_t id)
{
 format_fixture *f=(format_fixture*)opaque;unsigned i;
 for(i=0;i<f->precise_count;++i)if(f->precise[i]==id)return 1;
 if(!f->planning || i>=1024){f->failed=1;return 0;}
 f->precise[f->precise_count++]=id;return 1;
}
static void format_put(format_fixture *f,unsigned op,uint32_t type,uint32_t result,
 unsigned count,uint32_t a,uint32_t b,uint32_t c)
{
 uint32_t words[3];words[0]=a;words[1]=b;words[2]=c;
 if(f->out)xe_spv_binary_instruction(f->out,op,type,result,words,count);
}
static uint32_t format_binary(format_fixture *f,unsigned op,uint32_t type,uint32_t a,uint32_t b)
{
 uint32_t id=f->next++;format_put(f,op,type,id,2,a,b,0);return id;
}
static uint32_t format_load(format_fixture *f,uint32_t base,unsigned lane)
{
 uint32_t index=format_binary(f,SpvOpIAdd,FU,base,format_constant(f,0,lane));
 uint32_t pointer=f->next++,id=f->next++;
 format_put(f,SpvOpAccessChain,FPU,pointer,3,FIN,format_constant(f,0,0),index);
 format_put(f,SpvOpLoad,FU,id,1,pointer,0,0);return id;
}
static int format_body(format_fixture *f)
{
 xe_transfer_format_spv s;xe_transfer_format_emitter e;uintptr_t input[4],values[8];
 uint32_t id,index,in_length,out_length,in_count,out_count,a,b,valid,base,dest,selector,pointer;
 uint32_t cases[46];unsigned kind,lane;
 memset(&s,0,sizeof(s));s.out=f->out;s.next=&f->next;s.uint_type=FU;s.float_type=FF;s.bool_type=FB;
 s.extended_set=FEXT;s.constant=format_constant;s.precise=format_precise;s.owner=f;
 e.emit=xe_transfer_format_spv_emit;e.context=&s;e.failed=0;
 format_put(f,SpvOpFunction,FV,FMAIN,2,0,FFT,0);format_put(f,SpvOpLabel,0,FENTRY,0,0,0,0);
 id=f->next++;format_put(f,SpvOpLoad,FV3,id,1,FINV,0,0);
 index=f->next++;format_put(f,SpvOpCompositeExtract,FU,index,2,id,0,0);
 in_length=f->next++;out_length=f->next++;
 format_put(f,SpvOpArrayLength,FU,in_length,2,FIN,0,0);format_put(f,SpvOpArrayLength,FU,out_length,2,FOUT,0,0);
 in_count=format_binary(f,SpvOpUDiv,FU,in_length,format_constant(f,0,32));
 out_count=format_binary(f,SpvOpUDiv,FU,out_length,format_constant(f,0,16));
 a=format_binary(f,SpvOpULessThan,FB,index,in_count);b=format_binary(f,SpvOpULessThan,FB,index,out_count);
 valid=format_binary(f,SpvOpLogicalAnd,FB,a,b);
 format_put(f,SpvOpSelectionMerge,0,0,2,FEND,0,0);format_put(f,SpvOpBranchConditional,0,0,3,valid,FWORK,FEND);
 format_put(f,SpvOpLabel,0,FWORK,0,0,0,0);
 base=format_binary(f,SpvOpIMul,FU,index,format_constant(f,0,32));
 dest=format_binary(f,SpvOpIMul,FU,index,format_constant(f,0,16));selector=format_load(f,base,0);
 for(lane=0;lane<4;++lane)input[lane]=format_load(f,base,lane+1);
 cases[0]=selector;cases[1]=FMERGE;
 for(kind=0;kind<22;++kind){cases[2+kind*2]=kind;cases[3+kind*2]=FCASE+kind;}
 format_put(f,SpvOpSelectionMerge,0,0,2,FMERGE,0,0);
 if(f->out)xe_spv_binary_instruction(f->out,SpvOpSwitch,0,0,cases,46);
 for(kind=0;kind<22;++kind){
  format_put(f,SpvOpLabel,0,FCASE+kind,0,0,0,0);
  if(!xe_format_values(&e,kind,input,values))return 0;
  for(lane=0;lane<8;++lane){
   a=format_binary(f,SpvOpIAdd,FU,dest,format_constant(f,0,lane));pointer=f->next++;
   format_put(f,SpvOpAccessChain,FPU,pointer,3,FOUT,format_constant(f,0,0),a);
   format_put(f,SpvOpStore,0,0,2,pointer,(uint32_t)values[lane],0);
  }
  format_put(f,SpvOpBranch,0,0,1,FMERGE,0,0);
 }
 format_put(f,SpvOpLabel,0,FMERGE,0,0,0,0);format_put(f,SpvOpBranch,0,0,1,FEND,0,0);
 format_put(f,SpvOpLabel,0,FEND,0,0,0,0);format_put(f,SpvOpReturn,0,0,0,0,0,0);
 format_put(f,SpvOpFunctionEnd,0,0,0,0,0,0);
 return !f->failed && !s.failed && (!f->out || !f->out->failed);
}
int xe_test_transfer_format_spv(xe_spv_binary *out)
{
 format_fixture f;uint32_t bound,header[5]={0x07230203,0x00010000,0,0,0};
 uint32_t entry[5]={SpvExecutionModelGLCompute,FMAIN,0x6e69616d,0,FINV};
 uint32_t mode[5]={FMAIN,SpvExecutionModeLocalSize,64,1,1};unsigned i;
 memset(&f,0,sizeof(f));f.planning=1;f.next=FDYNAMIC;
 if(!out || !format_body(&f))return 0;
 bound=f.next;header[3]=bound;f.planning=0;f.next=FDYNAMIC;f.out=out;
 xe_spv_binary_reset(out);if(!xe_spv_binary_reserve_exact(out,65536))return 0;
 xe_spv_binary_append(out,header,5);format_put(&f,SpvOpCapability,0,0,1,SpvCapabilityShader,0,0);
 {uint32_t prefix=FEXT;xe_spv_binary_string_span(out,SpvOpExtInstImport,&prefix,1,"GLSL.std.450",12);}
 format_put(&f,SpvOpMemoryModel,0,0,2,SpvAddressingModelLogical,SpvMemoryModelGLSL450,0);
 xe_spv_binary_instruction(out,SpvOpEntryPoint,0,0,entry,5);xe_spv_binary_instruction(out,SpvOpExecutionMode,0,0,mode,5);
 format_put(&f,SpvOpDecorate,0,0,3,FINV,SpvDecorationBuiltIn,SpvBuiltInGlobalInvocationId);
 format_put(&f,SpvOpDecorate,0,0,3,FA,SpvDecorationArrayStride,4);
 format_put(&f,SpvOpDecorate,0,0,2,FS,SpvDecorationBufferBlock,0);
 {uint32_t member[4]={FS,0,SpvDecorationOffset,0};xe_spv_binary_instruction(out,SpvOpMemberDecorate,0,0,member,4);}
 for(i=0;i<2;++i){format_put(&f,SpvOpDecorate,0,0,3,FIN+i,SpvDecorationDescriptorSet,0);
  format_put(&f,SpvOpDecorate,0,0,3,FIN+i,SpvDecorationBinding,i);}
 for(i=0;i<f.precise_count;++i)format_put(&f,SpvOpDecorate,0,0,2,f.precise[i],SpvDecorationNoContraction,0);
 format_put(&f,SpvOpTypeVoid,0,FV,0,0,0,0);format_put(&f,SpvOpTypeInt,0,FU,2,32,0,0);
 format_put(&f,SpvOpTypeFloat,0,FF,1,32,0,0);format_put(&f,SpvOpTypeBool,0,FB,0,0,0,0);
 format_put(&f,SpvOpTypeVector,0,FV3,2,FU,3,0);format_put(&f,SpvOpTypeRuntimeArray,0,FA,1,FU,0,0);
 format_put(&f,SpvOpTypeStruct,0,FS,1,FA,0,0);format_put(&f,SpvOpTypePointer,0,FPB,2,SpvStorageClassUniform,FS,0);
 format_put(&f,SpvOpTypePointer,0,FPU,2,SpvStorageClassUniform,FU,0);format_put(&f,SpvOpTypePointer,0,FPI,2,SpvStorageClassInput,FV3,0);
 format_put(&f,SpvOpTypeFunction,0,FFT,1,FV,0,0);
 for(i=0;i<f.constant_count;++i)format_put(&f,SpvOpConstant,f.constants[i].floating?FF:FU,FCONST+i,1,f.constants[i].bits,0,0);
 format_put(&f,SpvOpVariable,FPB,FIN,1,SpvStorageClassUniform,0,0);format_put(&f,SpvOpVariable,FPB,FOUT,1,SpvStorageClassUniform,0,0);
 format_put(&f,SpvOpVariable,FPI,FINV,1,SpvStorageClassInput,0,0);
 if(!format_body(&f) || f.next!=bound || out->capacity!=65536)return 0;
 printf("FORMAT_SPV words=%lu constants=%u precise=%u final-only-allocation=1\n",(unsigned long)out->count,f.constant_count,f.precise_count);return 1;
}
int main(int argc,char **argv)
{
 xe_spv_binary out={0};FILE *file;int ok;
 (void)xe_sample_input;(void)xe_sample_expected;
 if(argc!=2 || xe_transfer_format_cpu_goldens() || !xe_test_transfer_format_spv(&out))return 1;
 file=fopen(argv[1],"wb");if(!file){xe_spv_binary_close(&out);return 1;}
 ok=fwrite(out.words,sizeof(*out.words),out.count,file)==out.count;
 if(fclose(file))ok=0;
 xe_spv_binary_close(&out);return ok?0:1;
}
