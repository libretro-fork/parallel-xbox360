/* Actual native parent emitter PSOs; test-only, BSD; MSVC C89. */
static void close_legacy_bank(bank *b) {
 unsigned n,v,d,a,s;
 for(n=0;n<2;++n)for(v=0;v<4;++v)for(d=0;d<3;++d)for(a=0;a<3;++a)for(s=0;s<3;++s)
  if(b->legacy[n][v][d][a][s])ID3D12PipelineState_Release(b->legacy[n][v][d][a][s]);
 if(b->legacy_root)ID3D12RootSignature_Release(b->legacy_root);
}
static HRESULT make_legacy_bank(ID3D12Device *device,bank *b,const char *prefix,
 int native2,const D3D12_SHADER_BYTECODE *vertex) {
 D3D12_ROOT_SIGNATURE_DESC root;D3D12_ROOT_PARAMETER parameters[2];D3D12_DESCRIPTOR_RANGE range;
 D3D12_GRAPHICS_PIPELINE_STATE_DESC desc;D3D12_INPUT_ELEMENT_DESC input;
 ID3DBlob *serialized=NULL,*errors=NULL;void *pixel=NULL;size_t bytes;
 unsigned n,v,d,a,s,samples;char path[4096];HRESULT status=E_FAIL;
 if(!prefix || strlen(prefix)>2048)return E_INVALIDARG;
 memset(&root,0,sizeof(root));memset(parameters,0,sizeof(parameters));memset(&range,0,sizeof(range));
 range.RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV;range.NumDescriptors=1;
 parameters[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
 parameters[0].DescriptorTable.NumDescriptorRanges=1;parameters[0].DescriptorTable.pDescriptorRanges=&range;
 parameters[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
 parameters[0].ShaderVisibility=parameters[1].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
 parameters[1].Constants.ShaderRegister=1;parameters[1].Constants.RegisterSpace=31;
 parameters[1].Constants.Num32BitValues=4;
 root.NumParameters=2;root.pParameters=parameters;root.Flags=D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
 status=D3D12SerializeRootSignature(&root,D3D_ROOT_SIGNATURE_VERSION_1,&serialized,&errors);
 if(FAILED(status))goto done;
 status=ID3D12Device_CreateRootSignature(device,0,ID3D10Blob_GetBufferPointer(serialized),ID3D10Blob_GetBufferSize(serialized),&IID_ID3D12RootSignature,(void**)&b->legacy_root);
 if(FAILED(status))goto done;
 memset(&desc,0,sizeof(desc));memset(&input,0,sizeof(input));desc.VS=*vertex;desc.pRootSignature=b->legacy_root;
 input.SemanticName="POSITION";input.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;
 input.InputSlotClass=D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA;input.InstanceDataStepRate=1;
 desc.InputLayout.NumElements=1;desc.InputLayout.pInputElementDescs=&input;
 desc.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
 desc.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;desc.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;
 desc.RasterizerState.DepthClipEnable=TRUE;desc.BlendState.RenderTarget[0].RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
 desc.NumRenderTargets=1;
 for(n=0;n<(native2?2u:1u);++n)for(v=0;v<4;++v)for(d=0;d<3;++d)for(a=0;a<3;++a)for(s=0;s<3;++s) {
  samples=d==1 && !n?4:1u<<d;desc.SampleDesc.Count=samples;
  desc.SampleMask=d==1 && !n?9u:(1u<<samples)-1;desc.RTVFormats[0]=numeric_format(v);
  sprintf(path,"%s-n%u-v%u-sm%u-dm%u-s%u-mode0.dxil",prefix,n,v,a,d,1u<<s);
  pixel=read_binary(path,&bytes);if(!pixel){status=E_FAIL;goto done;}
  desc.PS.pShaderBytecode=pixel;desc.PS.BytecodeLength=bytes;
  status=ID3D12Device_CreateGraphicsPipelineState(device,&desc,&IID_ID3D12PipelineState,(void**)&b->legacy[n][v][d][a][s]);
  free(pixel);pixel=NULL;if(FAILED(status))goto done;
 }
 status=S_OK;
done:
 free(pixel);if(errors)ID3D10Blob_Release(errors);if(serialized)ID3D10Blob_Release(serialized);return status;
}
