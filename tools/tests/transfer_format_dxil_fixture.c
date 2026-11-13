/* Run the existing native golden fixture through the portable raw-DXIL
 * adapter, without duplicating its emitter or expected packet tables. C89. */
#include "edram_format.h"
#include "transfer_format.h"
#include "guest_resources.h"
#include "dxil_test_storage.h"
#include <dxil_module.h>
#include <dxil_function.h>
static XeDxilValue portable_small(XeDxilAlu *alu,XeDxilValue value,int unpack)
{
   xe_transfer_format_emitter e;
   if(!xe_dxil_transfer_format_init(&e,alu))return NULL;
   return (XeDxilValue)(unpack?xe_transfer_format_unpack7e3(&e,(uintptr_t)value):
         xe_transfer_format_pack7e3(&e,(uintptr_t)value));
}
static XeDxilValue portable_pack7e3(XeDxilAlu *a,XeDxilValue v) { return portable_small(a,v,0); }
static XeDxilValue portable_unpack7e3(XeDxilAlu *a,XeDxilValue v) { return portable_small(a,v,1); }
static XeDxilValue portable_pack_depth(XeDxilAlu *alu,XeDxilValue v,int round,int remap)
{
   xe_transfer_format_emitter e;
   if(!xe_dxil_transfer_format_init(&e,alu))return NULL;
   return (XeDxilValue)xe_transfer_format_pack20e4(&e,(uintptr_t)v,(unsigned)round,(unsigned)remap);
}
static XeDxilValue portable_unpack_depth(XeDxilAlu *alu,XeDxilValue v,int remap)
{
   xe_transfer_format_emitter e;
   if(!xe_dxil_transfer_format_init(&e,alu))return NULL;
   return (XeDxilValue)xe_transfer_format_unpack20e4(&e,(uintptr_t)v,(unsigned)remap);
}
static int portable_pack_color(XeDxilAlu *alu,unsigned format,int integer,XeDxilValue source[4],XeDxilValue out[2])
{
   xe_transfer_format_emitter e;
   uintptr_t input[4],output[2];
   unsigned i,count;
   if(!xe_dxil_transfer_format_init(&e,alu))return 0;
   for(i=0;i<4;++i)input[i]=(uintptr_t)source[i];
   count=xe_transfer_format_pack_color(&e,format,(unsigned)integer,input,output);
   if(!count)return 0;
   for(i=0;i<2;++i)out[i]=(XeDxilValue)output[i];
   return (int)count;
}
static int portable_unpack_color(XeDxilAlu *alu,unsigned format,int integer,XeDxilValue source[2],XeDxilValue out[4])
{
   xe_transfer_format_emitter e;
   uintptr_t input[2],output[4];
   unsigned i,count;
   if(!xe_dxil_transfer_format_init(&e,alu))return 0;
   for(i=0;i<2;++i)input[i]=(uintptr_t)source[i];
   count=xe_transfer_format_unpack_color(&e,format,(unsigned)integer,input,output);
   if(!count)return 0;
   for(i=0;i<4;++i)out[i]=(XeDxilValue)output[i];
   return (int)count;
}
static int raw_constant_bits(struct dxil_module *m,XeDxilAlu *alu,XeDxilValue handle,unsigned index,FILE *golden)
{
   static const uint32_t patterns[]={0,0x80000000,1,0x80000001,0x7fc12345,0xffc54321,0x7fa12345,0x7f800000,0xff800000};
   xe_transfer_format_emitter e;
   XeDxilValue value[4];uint32_t expected[8];unsigned i,j;
   (void)m;
   if(!xe_dxil_transfer_format_init(&e,alu))return 0;
   for(i=0;i<sizeof(patterns)/sizeof(patterns[0]);++i){
      memset(expected,0,sizeof(expected));
      for(j=0;j<4;++j){
         value[j]=(XeDxilValue)xe_tf_op(&e,XE_TF_F32_BITS,patterns[i],0,0);
         expected[j]=patterns[i];
      }
      expected[7]=UINT32_C(0xb5000000)|i;
      if(!xe_dxil_test_record(alu->module,handle,index+i,alu,value,expected,golden))return 0;
   }
   return 1;
}
#define xe_dxil_pack_float7e3 portable_pack7e3
#define xe_dxil_unpack_float7e3 portable_unpack7e3
#define xe_dxil_pack_depth20e4 portable_pack_depth
#define xe_dxil_unpack_depth20e4 portable_unpack_depth
#define xe_dxil_pack_edram_color portable_pack_color
#define xe_dxil_unpack_edram_color portable_unpack_color
#define XE_DXIL_EDRAM_FORMAT_EXTRA raw_constant_bits
#include "dxil_edram_format_test.c"
