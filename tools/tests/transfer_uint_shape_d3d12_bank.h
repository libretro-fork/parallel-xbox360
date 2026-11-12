/* Prepared test pipelines. All handles exist before the image-case loop. */
#ifndef XE_TEST_TRANSFER_UINT_SHAPE_D3D12_BANK_H
#define XE_TEST_TRANSFER_UINT_SHAPE_D3D12_BANK_H
static HRESULT make_shaped_bank(ID3D12Device *device,bank *b,const char *prefix,
 int native2,const D3D12_GRAPHICS_PIPELINE_STATE_DESC *base) {
 D3D12_GRAPHICS_PIPELINE_STATE_DESC desc=*base;ID3D12PipelineState *created;
 char path[4096];blob pixel={0};unsigned n,c,f,sm,dm,mode,physical,variant,samples;HRESULT status;
 desc.pRootSignature=b->transfer_root;
 for(n=0;n<(native2?2u:1u);++n)for(c=0;c<6;++c)for(f=0;f<4;++f)
 for(sm=0;sm<3;++sm)for(dm=0;dm<3;++dm)for(mode=0;mode<2;++mode) {
  if(!dm && mode)continue;
  samples=dm==1 && !n?4:1u<<dm;
  desc.SampleDesc.Count=samples;desc.RTVFormats[0]=format(f==0?4:f==1?5:f==2?14:15);
  sprintf(path,"%s-shape-n%u-c%u-f%u-s%u-d%u-m%u.dxil",prefix,n,c,f,sm,dm,mode);
  pixel.data=read_binary(path,&pixel.bytes);if(!pixel.data)return E_FAIL;
  desc.PS.pShaderBytecode=pixel.data;desc.PS.BytecodeLength=pixel.bytes;
  for(physical=0;physical<(mode?samples:1);++physical) {
   if(mode && dm==1 && !n && physical!=0 && physical!=3)continue;
   variant=!dm?0:mode?physical+2:dm==1 && !n?1:0;
   desc.SampleMask=!dm?1:mode?1u<<physical:dm==1 && !n?9:UINT32_MAX;
   created=NULL;
   status=ID3D12Device_CreateGraphicsPipelineState(device,&desc,&IID_ID3D12PipelineState,(void**)&created);
   if(FAILED(status)){free(pixel.data);return status;}
   if(!created){free(pixel.data);return E_FAIL;}
   b->shaped[n][c][f][dm][sm][variant]=created;
  }
  free(pixel.data);pixel.data=NULL;
 }
 return S_OK;
}
static void close_shaped_bank(bank *b) {
 unsigned n,c,f,sm,dm,v;
 for(n=0;n<2;++n)for(c=0;c<6;++c)for(f=0;f<4;++f)
 for(sm=0;sm<3;++sm)for(dm=0;dm<3;++dm)for(v=0;v<6;++v)
  if(b->shaped[n][c][f][dm][sm][v])ID3D12PipelineState_Release(b->shaped[n][c][f][dm][sm][v]);
}
#endif
