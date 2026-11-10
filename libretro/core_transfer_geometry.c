/* Procedural transfer vertices: preserve the old two triangles exactly. C89. */
#include "core_transfer_geometry.h"
#include <spirv/unified1/spirv.h>
enum {TV=1,TF,T4,TU,TB,TFN,PIF4,PIU,POF4,F0,F1,U0,U1,MX,MY,
 BBOX,VERTEX,POSITION,MAIN,LABEL,BOX,INDEX,SX,BX,CX,SY,BY,CY,
 X0,Y0,X1,Y1,X,Y,POS,BOUND};
static void put(xe_spv_binary *b,unsigned op,uint32_t type,uint32_t result,
 unsigned n,uint32_t a,uint32_t c,uint32_t d,uint32_t e) {
 uint32_t v[4];v[0]=a;v[1]=c;v[2]=d;v[3]=e;
 xe_spv_binary_instruction(b,op,type,result,v,n);
}
int xe_transfer_rectangle_spv(xe_spv_binary *b) {
 uint32_t entry[7]={SpvExecutionModelVertex,MAIN,0x6e69616du,0,BBOX,VERTEX,POSITION};
 uint32_t header[5]={0x07230203u,0x00010000u,0,BOUND,0};
 if(!b)return 0;
 xe_spv_binary_reset(b);if(!xe_spv_binary_reserve(b,256))return 0;
 xe_spv_binary_append(b,header,5);
 put(b,SpvOpCapability,0,0,1,SpvCapabilityShader,0,0,0);
 put(b,SpvOpMemoryModel,0,0,2,SpvAddressingModelLogical,SpvMemoryModelGLSL450,0,0);
 xe_spv_binary_instruction(b,SpvOpEntryPoint,0,0,entry,7);
 put(b,SpvOpDecorate,0,0,3,BBOX,SpvDecorationLocation,0,0);
 put(b,SpvOpDecorate,0,0,3,VERTEX,SpvDecorationBuiltIn,SpvBuiltInVertexIndex,0);
 put(b,SpvOpDecorate,0,0,3,POSITION,SpvDecorationBuiltIn,SpvBuiltInPosition,0);
 put(b,SpvOpTypeVoid,0,TV,0,0,0,0,0);
 put(b,SpvOpTypeFloat,0,TF,1,32,0,0,0);
 put(b,SpvOpTypeVector,0,T4,2,TF,4,0,0);
 put(b,SpvOpTypeInt,0,TU,2,32,0,0,0);
 put(b,SpvOpTypeBool,0,TB,0,0,0,0,0);
 put(b,SpvOpTypeFunction,0,TFN,1,TV,0,0,0);
 put(b,SpvOpTypePointer,0,PIF4,2,SpvStorageClassInput,T4,0,0);
 put(b,SpvOpTypePointer,0,PIU,2,SpvStorageClassInput,TU,0,0);
 put(b,SpvOpTypePointer,0,POF4,2,SpvStorageClassOutput,T4,0,0);
 put(b,SpvOpConstant,TF,F0,1,0,0,0,0);put(b,SpvOpConstant,TF,F1,1,0x3f800000u,0,0,0);
 put(b,SpvOpConstant,TU,U0,1,0,0,0,0);put(b,SpvOpConstant,TU,U1,1,1,0,0,0);
 put(b,SpvOpConstant,TU,MX,1,XE_TRANSFER_VK_X_MASK,0,0,0);
 put(b,SpvOpConstant,TU,MY,1,XE_TRANSFER_VK_Y_MASK,0,0,0);
 put(b,SpvOpVariable,PIF4,BBOX,1,SpvStorageClassInput,0,0,0);
 put(b,SpvOpVariable,PIU,VERTEX,1,SpvStorageClassInput,0,0,0);
 put(b,SpvOpVariable,POF4,POSITION,1,SpvStorageClassOutput,0,0,0);
 put(b,SpvOpFunction,TV,MAIN,2,0,TFN,0,0);put(b,SpvOpLabel,0,LABEL,0,0,0,0,0);
 put(b,SpvOpLoad,T4,BOX,1,BBOX,0,0,0);put(b,SpvOpLoad,TU,INDEX,1,VERTEX,0,0,0);
 put(b,SpvOpShiftRightLogical,TU,SX,2,MX,INDEX,0,0);put(b,SpvOpBitwiseAnd,TU,BX,2,SX,U1,0,0);
 put(b,SpvOpINotEqual,TB,CX,2,BX,U0,0,0);
 put(b,SpvOpShiftRightLogical,TU,SY,2,MY,INDEX,0,0);put(b,SpvOpBitwiseAnd,TU,BY,2,SY,U1,0,0);
 put(b,SpvOpINotEqual,TB,CY,2,BY,U0,0,0);
 put(b,SpvOpCompositeExtract,TF,X0,2,BOX,0,0,0);put(b,SpvOpCompositeExtract,TF,Y0,2,BOX,1,0,0);
 put(b,SpvOpCompositeExtract,TF,X1,2,BOX,2,0,0);put(b,SpvOpCompositeExtract,TF,Y1,2,BOX,3,0,0);
 put(b,SpvOpSelect,TF,X,3,CX,X1,X0,0);put(b,SpvOpSelect,TF,Y,3,CY,Y1,Y0,0);
 put(b,SpvOpCompositeConstruct,T4,POS,4,X,Y,F0,F1);put(b,SpvOpStore,0,0,2,POSITION,POS,0,0);
 put(b,SpvOpReturn,0,0,0,0,0,0,0);put(b,SpvOpFunctionEnd,0,0,0,0,0,0,0);
 if(b->failed){b->count=0;return 0;}return 1;
}
