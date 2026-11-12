/* Actual production numeric bank construction for the independent oracle.
 * VS bytes and callbacks are borrowed through initialization. BSD; C89. */
static HRESULT make_identity_bank(ID3D12Device *device,bank *b,int native2,
 const D3D12_SHADER_BYTECODE *vertex)
{
 xe_transfer_identity_d3d12_config config={0};xe_transfer_identity_d3d12_ops ops;
 unsigned n,scale,f;
 if(!xe_transfer_identity_d3d12_native_ops(&ops))return E_FAIL;
 config.device=device;config.vertex=vertex->pShaderBytecode;config.vertex_bytes=vertex->BytecodeLength;
 for(f=0;f<4;++f)config.sample_mask[f]=7;
 for(n=0;n<(native2?2u:1u);++n)for(scale=0;scale<3;++scale) {
  config.native2=n;
  config.scale_x=config.scale_y=1u<<scale;
  if(!xe_transfer_identity_d3d12_init(&b->production[n][scale],&config,&ops))return E_FAIL;
 }
 return S_OK;
}
static void close_identity_bank(bank *b)
{
 xe_transfer_identity_d3d12_ops ops;unsigned n,scale;
 xe_transfer_identity_d3d12_native_ops(&ops);
 for(n=0;n<2;++n)for(scale=0;scale<4;++scale)
  if(!xe_transfer_identity_d3d12_close(&b->production[n][scale],&ops))exit(1);
}
