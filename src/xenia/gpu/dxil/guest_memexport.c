/* Native guest export packing and shared-memory stores. MSVC C89; BSD license.
 * Guest format and address rules follow the owned Xenia memory exporter. */
#include "guest_memexport.h"
#include "edram_format.h"
#include <dxil_module.h>
#include <dxil_function.h>

static XeDxilValue ic(XeDxilAlu *a, unsigned v) { return dxil_module_get_int32_const(a->module,(int32_t)v); }
static XeDxilValue fc(XeDxilAlu *a, float v) { return dxil_module_get_float_const(a->module,v); }
static XeDxilValue bin(XeDxilAlu *a, enum dxil_bin_opcode op, XeDxilValue x, XeDxilValue y) {
  XeDxilValue v = x && y ? dxil_emit_binop(a->module,op,x,y,0) : NULL;
  if (!v) a->failed = 1;
  return v;
}
static XeDxilValue cmp(XeDxilAlu *a, enum dxil_cmp_pred op, XeDxilValue x, XeDxilValue y) {
  XeDxilValue v = x && y ? dxil_emit_cmp(a->module,op,x,y) : NULL;
  if (!v) a->failed = 1;
  return v;
}
static XeDxilValue pick(XeDxilAlu *a, XeDxilValue c, XeDxilValue x, XeDxilValue y) {
  XeDxilValue v = c && x && y ? dxil_emit_select(a->module,c,x,y) : NULL;
  if (!v) a->failed = 1;
  return v;
}
static XeDxilValue cast(XeDxilAlu *a, enum dxil_cast_opcode op, XeDxilValue x) {
  XeDxilValue v = x ? dxil_emit_cast(a->module,op,dxil_module_get_int_type(a->module,32),x) : NULL;
  if (!v) a->failed = 1;
  return v;
}
static XeDxilValue field(XeDxilAlu *a, XeDxilValue v, unsigned offset, unsigned mask) {
  return bin(a,DXIL_BINOP_AND,bin(a,DXIL_BINOP_LSHR,v,ic(a,offset)),ic(a,mask));
}
static XeDxilValue flag(XeDxilAlu *a, XeDxilValue v, unsigned mask) { return cmp(a,DXIL_ICMP_NE,bin(a,DXIL_BINOP_AND,v,ic(a,mask)),ic(a,0)); }
static unsigned schema(unsigned format, unsigned widths[4], unsigned *kind) {
  unsigned i, bytes = 0;
  for (i = 0; i < 4; ++i) widths[i] = 0;
  *kind = 0;
  switch (format) {
    case 2: case 8: case 9: widths[0] = 8; break;
    case 3: widths[0] = widths[1] = widths[2] = 5; widths[3] = 1; break;
    case 4: widths[0] = widths[2] = 5; widths[1] = 6; break;
    case 5: widths[0] = widths[1] = 5; widths[2] = 6; break;
    case 6: case 14: case 50: widths[0] = widths[1] = widths[2] = widths[3] = 8; break;
    case 7: case 54: widths[0] = widths[1] = widths[2] = 10; widths[3] = 2; break;
    case 10: widths[0] = widths[1] = 8; break;
    case 15: widths[0] = widths[1] = widths[2] = widths[3] = 4; break;
    case 16: case 55: widths[0] = widths[1] = 11; widths[2] = 10; break;
    case 17: case 56: widths[0] = 10; widths[1] = widths[2] = 11; break;
    case 24: widths[0] = 16; break;
    case 25: widths[0] = widths[1] = 16; break;
    case 26: widths[0] = widths[1] = widths[2] = widths[3] = 16; break;
    case 30: *kind = 1; widths[0] = 16; break;
    case 31: *kind = 1; widths[0] = widths[1] = 16; break;
    case 32: *kind = 1; widths[0] = widths[1] = widths[2] = widths[3] = 16; break;
    case 36: *kind = 2; widths[0] = 32; break;
    case 37: *kind = 2; widths[0] = widths[1] = 32; break;
    case 38: *kind = 2; widths[0] = widths[1] = widths[2] = widths[3] = 32; break;
    default: return 0;
  }
  for (i = 0; i < 4; ++i) bytes += widths[i];
  return bytes/8;
}
unsigned xe_dxil_memexport_pack(XeDxilAlu *a, unsigned format,
    XeDxilValue info, XeDxilValue source[4], XeDxilValue packed[4]) {
  XeDxilValue values[4], halves[2], v, is_signed, norm, swapped, lo, hi, magnitude, signed_v, unsigned_v;
  unsigned widths[4], kind, bytes, i, bit = 0, word;
  if (!a || !a->module || !info || !source || !packed) return 0;
  bytes = schema(format,widths,&kind); if (!bytes) return 0;
  is_signed = flag(a,info,1u<<16); norm = cmp(a,DXIL_ICMP_EQ,bin(a,DXIL_BINOP_AND,info,ic(a,1u<<17)),ic(a,0));
  swapped = flag(a,info,1u<<19);
  for (i = 0; i < 4; ++i) {
    values[i] = i == 0 || i == 2 ? pick(a,swapped,source[2-i],source[i]) : source[i];
    packed[i] = ic(a,0);
  }
  if (kind == 2) {
    for (i = 0; i < 4; ++i) packed[i] = cast(a,DXIL_CAST_BITCAST,values[i]);
  } else if (kind == 1) {
    for (i = 0; i < 4; ++i) if (!widths[i]) values[i] = fc(a,0);
    if (!xe_dxil_pack_render_color(a,7,values,halves)) return 0;
    packed[0] = halves[0]; packed[1] = halves[1];
  } else {
    for (i = 0; i < 4 && widths[i]; ++i) {
      v = pick(a,cmp(a,DXIL_FCMP_UNO,values[i],values[i]),fc(a,0),values[i]);
      lo = pick(a,is_signed,fc(a,-1),fc(a,0)); hi = fc(a,1);
      magnitude = pick(a,is_signed,fc(a,(float)((1u<<(widths[i]-1))-1)),fc(a,(float)((1u<<widths[i])-1)));
      lo = pick(a,cmp(a,DXIL_FCMP_OLT,v,lo),lo,v); hi = pick(a,cmp(a,DXIL_FCMP_OGT,lo,hi),hi,lo);
      v = pick(a,norm,bin(a,DXIL_BINOP_MUL,hi,magnitude),v);
      /* D3D integer conversion rounds halves away from zero. */
      signed_v = cast(a,DXIL_CAST_FPTOSI,bin(a,DXIL_BINOP_ADD,v,pick(a,cmp(a,DXIL_FCMP_OLT,v,fc(a,0)),fc(a,-.5f),fc(a,.5f))));
      unsigned_v = cast(a,DXIL_CAST_FPTOUI,bin(a,DXIL_BINOP_ADD,v,fc(a,.5f)));
      v = pick(a,is_signed,signed_v,unsigned_v);
      word = bit/32;
      if (bit%32) v = bin(a,DXIL_BINOP_SHL,bin(a,DXIL_BINOP_AND,v,ic(a,(1u<<widths[i])-1)),ic(a,bit%32));
      if (!(bit%32)) packed[word] = v;
      else {
        packed[word] = bin(a,DXIL_BINOP_AND,packed[word],ic(a,~(((1u<<widths[i])-1)<<(bit%32))));
        packed[word] = bin(a,DXIL_BINOP_OR,packed[word],v);
      }
      bit += widths[i];
    }
  }
  return a->failed ? 0 : bytes;
}
static void begin(XeDxilAlu *a, XeDxilAluIo *io, unsigned block) {
  a->module->cur_emitting_func->basic_block_ids[block] = (int)a->module->cur_emitting_func->curr_block;
  *io->current_block = block;
}
static int conditional(XeDxilAlu *a, XeDxilAluIo *io, XeDxilValue condition, unsigned *done) {
  unsigned blocks;
  if (!condition || !dxil_function_reserve_blocks(a->module,2,&blocks) || !dxil_emit_branch(a->module,condition,blocks,blocks+1)) return 0;
  begin(a,io,blocks); *done = blocks+1; return 1;
}
static int end(XeDxilAlu *a, XeDxilAluIo *io, unsigned done) {
  if (!dxil_emit_branch(a->module,NULL,done,0)) return 0;
  begin(a,io,done); return !a->failed;
}
static XeDxilValue swap32(XeDxilAlu *a, XeDxilValue v, XeDxilValue endian) {
  XeDxilValue swapped, condition;
  swapped = bin(a,DXIL_BINOP_OR,bin(a,DXIL_BINOP_SHL,bin(a,DXIL_BINOP_AND,v,ic(a,0x00ff00ff)),ic(a,8)),bin(a,DXIL_BINOP_LSHR,bin(a,DXIL_BINOP_AND,v,ic(a,0xff00ff00)),ic(a,8)));
  condition = bin(a,DXIL_BINOP_OR,cmp(a,DXIL_ICMP_EQ,endian,ic(a,1)),cmp(a,DXIL_ICMP_EQ,endian,ic(a,2)));
  v = pick(a,condition,swapped,v);
  swapped = bin(a,DXIL_BINOP_OR,bin(a,DXIL_BINOP_SHL,v,ic(a,16)),bin(a,DXIL_BINOP_LSHR,v,ic(a,16)));
  condition = bin(a,DXIL_BINOP_OR,cmp(a,DXIL_ICMP_EQ,endian,ic(a,2)),cmp(a,DXIL_ICMP_EQ,endian,ic(a,3)));
  return pick(a,condition,swapped,v);
}
static int atomic(XeDxilAlu *a, XeDxilValue uav, unsigned op, XeDxilValue address, XeDxilValue value) {
  XeDxilValue args[7]; const struct dxil_func *f = dxil_get_function(a->module,"dx.op.atomicBinOp",DXIL_I32);
  args[0] = ic(a,78); args[1] = uav; args[2] = ic(a,op); args[3] = address;
  args[4] = ic(a,0); args[5] = args[4]; args[6] = value;
  return f && value && dxil_emit_call(a->module,f,args,7) != NULL;
}
int xe_dxil_memexport_store(XeDxilAlu *a, XeDxilAluIo *io, XeDxilValue uav,
    XeDxilValue address[4], XeDxilValue data[5][4], unsigned potential,
    XeDxilValue written, XeDxilValue allowed) {
  static const unsigned formats[] = {2,3,4,5,6,7,10,15,16,17,24,25,26,30,31,32,36,37,38};
  static const unsigned shifts[4] = {30,23,20,23}, prefixes[4] = {1,0x96,0x4b0,0x96};
  XeDxilValue ea[4], valid, info, format, index, count, endian, reverse64, reverse128, normal_endian, packed[4], swapped[4], base, bytes, value, shift, mask;
  unsigned i, n, k, outer, done, element_done, size, log_size, widths[4], kind;
  if (!a || !io || !io->current_block || !uav || !address || !data || !written || potential > 31) return 0;
  if (!potential) return 1;
  valid = flag(a,written,potential);
  if (allowed) valid = bin(a,DXIL_BINOP_AND,valid,allowed);
  for (i = 0; i < 4; ++i) {
    ea[i] = cast(a,DXIL_CAST_BITCAST,address[i]);
    valid = bin(a,DXIL_BINOP_AND,valid,cmp(a,DXIL_ICMP_EQ,bin(a,DXIL_BINOP_LSHR,ea[i],ic(a,shifts[i])),ic(a,prefixes[i])));
  }
  if (!conditional(a,io,valid,&outer)) return 0;
  info = ea[2]; format = field(a,info,8,63); index = field(a,ea[1],0,0x7fffff); count = field(a,ea[3],0,0x7fffff);
  endian = field(a,info,0,7); reverse64 = cmp(a,DXIL_ICMP_EQ,endian,ic(a,4)); reverse128 = cmp(a,DXIL_ICMP_EQ,endian,ic(a,5));
  normal_endian = pick(a,bin(a,DXIL_BINOP_OR,reverse64,reverse128),ic(a,2),endian);
  for (k = 0; k < sizeof(formats)/sizeof(formats[0]); ++k) {
    size = schema(formats[k],widths,&kind); log_size = 0; for (i = size; i > 1; i >>= 1) ++log_size;
    valid = cmp(a,DXIL_ICMP_EQ,format,ic(a,formats[k]));
    /* Alias formats share one conversion/store body, including all eM lanes. */
    if (formats[k] == 2) valid = bin(a,DXIL_BINOP_OR,valid,bin(a,DXIL_BINOP_OR,cmp(a,DXIL_ICMP_EQ,format,ic(a,8)),cmp(a,DXIL_ICMP_EQ,format,ic(a,9))));
    if (formats[k] == 6) valid = bin(a,DXIL_BINOP_OR,valid,bin(a,DXIL_BINOP_OR,cmp(a,DXIL_ICMP_EQ,format,ic(a,14)),cmp(a,DXIL_ICMP_EQ,format,ic(a,50))));
    if (formats[k] == 7 || formats[k] == 16 || formats[k] == 17) valid = bin(a,DXIL_BINOP_OR,valid,cmp(a,DXIL_ICMP_EQ,format,ic(a,formats[k] == 7 ? 54 : formats[k] == 16 ? 55 : 56)));
    if (!conditional(a,io,valid,&done)) return 0;
    base = bin(a,DXIL_BINOP_AND,bin(a,DXIL_BINOP_ADD,bin(a,DXIL_BINOP_SHL,ea[0],ic(a,2)),bin(a,DXIL_BINOP_SHL,index,ic(a,log_size))),ic(a,0x1fffffff));
    for (n = 0; n < 5; ++n) if (potential&(1u<<n)) {
      valid = bin(a,DXIL_BINOP_AND,flag(a,written,1u<<n),cmp(a,DXIL_ICMP_ULT,bin(a,DXIL_BINOP_ADD,index,ic(a,n)),count));
      if (!conditional(a,io,valid,&element_done) || !xe_dxil_memexport_pack(a,formats[k],info,data[n],packed)) return 0;
      for (i = 0; i < 4; ++i) swapped[i] = swap32(a,pick(a,reverse128,packed[3-i],pick(a,reverse64,packed[i^1],packed[i])),normal_endian);
      bytes = bin(a,DXIL_BINOP_ADD,base,ic(a,n*size));
      if (size < 4) {
        shift = bin(a,DXIL_BINOP_MUL,bin(a,DXIL_BINOP_AND,bytes,ic(a,3)),ic(a,8));
        mask = bin(a,DXIL_BINOP_SHL,ic(a,size == 1 ? 255 : 65535),shift);
        value = bin(a,DXIL_BINOP_AND,bin(a,DXIL_BINOP_SHL,swapped[0],shift),mask);
        bytes = bin(a,DXIL_BINOP_AND,bytes,ic(a,~3u));
        if (!atomic(a,uav,1,bytes,bin(a,DXIL_BINOP_XOR,mask,ic(a,~0u))) || !atomic(a,uav,2,bytes,value)) return 0;
      } else for (i = 0; i < size/4; ++i) if (!xe_dxil_raw_buffer_store(a,uav,bin(a,DXIL_BINOP_ADD,bytes,ic(a,i*4)),swapped[i])) return 0;
      if (!end(a,io,element_done)) return 0;
    }
    if (!end(a,io,done)) return 0;
  }
  return end(a,io,outer);
}
