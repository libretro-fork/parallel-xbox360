/* Actual raw decoder checkpoint and PCM continuation. MSVC C89. */
#include <libavcodec/xmaframes_checkpoint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void *xe_xma_test_new(int,unsigned);
void xe_xma_test_free(void *);
struct AVCodecContext *xe_xma_test_context(void *);
unsigned char *xe_xma_test_packet(void *);
int xe_xma_test_send(void *,unsigned);
int xe_xma_test_receive(void *);
const void *xe_xma_test_samples(void *,unsigned);
static int view_of(void *p,FFXmaCheckpointView *out) {return ff_xmaframes_checkpoint_view(xe_xma_test_context(p),out);}
static int apply_to(void *p,const FFXmaCheckpointView *in) {return ff_xmaframes_checkpoint_apply(xe_xma_test_context(p),in);}
#define CHECK(x) do {if(!(x)) {fprintf(stderr,"xma checkpoint line %u\n",(unsigned)__LINE__);exit(1);}} while(0)
static void put_bits(unsigned char *p,unsigned *position,unsigned value,unsigned count) {
 unsigned i;
 for(i=count;i;--i) {if((value>>(i-1))&1) p[*position/8]|=(unsigned char)(1u<<(7-*position%8));++*position;}
}
static unsigned packet(unsigned char *p,unsigned channels,unsigned padding) {
 unsigned bits=channels==1?31:36,pos=8+padding;
 unsigned end=(8+padding+bits+7)/8;
 memset(p,0,80);
 p[0]=(unsigned char)((padding<<5)|((end*8-8-padding-bits)<<2));
 put_bits(p,&pos,bits,15);put_bits(p,&pos,1,1);put_bits(p,&pos,0,1);
 if(channels==2) put_bits(p,&pos,0,1);
 put_bits(p,&pos,0,8);put_bits(p,&pos,0,1);
 put_bits(p,&pos,0,1);put_bits(p,&pos,0,1);
 if(channels==2) {put_bits(p,&pos,0,1);put_bits(p,&pos,2,2);}
 put_bits(p,&pos,0,channels);put_bits(p,&pos,0,2);
 CHECK(pos==8+padding+bits);return end;
}
static void unchanged(void *ctx,const FFXmaCheckpointView *invalid) {
 FFXmaCheckpointView before,after;
 unsigned char bytes[2][512*sizeof(float)];
 unsigned c;
 CHECK(view_of(ctx,&before));
 for(c=0;c<before.state.channels;++c) memcpy(bytes[c],before.history[c],sizeof(bytes[c]));
 CHECK(!apply_to(ctx,invalid));
 CHECK(view_of(ctx,&after));
 CHECK(!memcmp(&before.state,&after.state,sizeof(before.state)));
 for(c=0;c<before.state.channels;++c) CHECK(!memcmp(bytes[c],after.history[c],sizeof(bytes[c])));
}
int main(void) {
 const int rates[4]={24000,32000,44100,48000};
 void *a,*b;
 unsigned packet_bytes;
 FFXmaCheckpointView seed,saved,view,bad;
 float history[2][512],record[2][512],reference[8][2][512];
 unsigned rate,channels,previous,c,i,n,nonzero=0,cases=0;
 uint32_t bits;
 CHECK(!ff_xmaframes_checkpoint_view(NULL,&view));
 CHECK(!ff_xmaframes_checkpoint_apply(NULL,&view));
 for(rate=0;rate<4;++rate) for(channels=1;channels<=2;++channels) for(previous=128;previous<=512;previous*=2) {
  a=xe_xma_test_new(rates[rate],channels);b=xe_xma_test_new(rates[rate],channels);CHECK(a && b);
  CHECK(view_of(a,&seed));
  seed.state.frame_number=UINT32_MAX-1;seed.state.trim_start=7;seed.state.trim_end=9;
  for(c=0;c<channels;++c) {
   for(i=0;i<512;++i) history[c][i]=(float)((int)(i%37)-18)*(float)(c+1)*0.001953125f;
   seed.history[c]=history[c];seed.history_bytes[c]=sizeof(history[c]);seed.state.previous_block[c]=previous;
  }
  CHECK(apply_to(a,&seed));
  CHECK(view_of(a,&saved));
  CHECK(saved.history[0]!=history[0]);
  for(c=0;c<channels;++c) {memcpy(record[c],saved.history[c],sizeof(record[c]));saved.history[c]=record[c];}
  bad=saved;bad.state.version=0;unchanged(b,&bad);
  bad=saved;bad.state.rate=1;unchanged(b,&bad);
  bad=saved;bad.state.channels=3;unchanged(b,&bad);
  bad=saved;bad.state.samples=1024;unchanged(b,&bad);
  bad=saved;bad.state.bit_exact^=1;unchanged(b,&bad);
  bad=saved;bad.state.previous_block[0]=192;unchanged(b,&bad);
  bad=saved;bad.state.trim_start=1024;unchanged(b,&bad);
  bad=saved;bad.state.trim_end=1024;unchanged(b,&bad);
  bad=saved;bad.state.skip_frame=2;unchanged(b,&bad);
  bad=saved;bad.state.packet_loss=2;unchanged(b,&bad);
  bad=saved;bad.history[0]=NULL;unchanged(b,&bad);
  bad=saved;bad.history_bytes[0]--;unchanged(b,&bad);
  bad=saved;bad.history[0]=(const void *)(UINTPTR_MAX-16);unchanged(b,&bad);
  if(channels==2) {
   /* Channel zero is valid: a bad second channel must not partially apply. */
   bad=saved;bad.history_bytes[1]--;unchanged(b,&bad);
   bad=saved;bad.state.previous_block[1]=192;unchanged(b,&bad);
  } else {
   bad=saved;bad.state.previous_block[1]=128;unchanged(b,&bad);
   bad=saved;bad.history[1]=record[0];unchanged(b,&bad);
   bad=saved;bad.history_bytes[1]=sizeof(record[0]);unchanged(b,&bad);
  }
  CHECK(view_of(b,&bad));unchanged(b,&bad);
  for(n=0;n<8;++n) {
   packet_bytes=packet(xe_xma_test_packet(a),channels,n);CHECK(xe_xma_test_send(a,packet_bytes)==0);
   CHECK(!view_of(a,&view));
   CHECK(!apply_to(a,&saved)); /* Pending output must survive rejected restore. */
   CHECK(xe_xma_test_receive(a)==512);
   for(c=0;c<channels;++c) {
    memcpy(reference[n][c],xe_xma_test_samples(a,c),sizeof(reference[n][c]));
    for(i=0;i<512;++i) if(reference[n][c][i]!=0.0f) ++nonzero;
   }
  }
  CHECK(apply_to(b,&saved));
  CHECK(view_of(b,&view));
  CHECK(!memcmp(&saved.state,&view.state,sizeof(saved.state)));
  for(c=0;c<channels;++c) CHECK(!memcmp(record[c],view.history[c],sizeof(record[c])));
  for(n=0;n<8;++n) {
   packet_bytes=packet(xe_xma_test_packet(b),channels,n);CHECK(xe_xma_test_send(b,packet_bytes)==0);
   CHECK(xe_xma_test_receive(b)==512);
   for(c=0;c<channels;++c) CHECK(!memcmp(reference[n][c],xe_xma_test_samples(b,c),sizeof(reference[n][c])));
  }
  /* Copy float payloads exactly, including values never normalized by state I/O. */
  bits=UINT32_C(0x80000000);memcpy(&record[0][0],&bits,4);
  bits=UINT32_C(0x7fc12345);memcpy(&record[0][1],&bits,4);
  CHECK(apply_to(b,&saved));CHECK(view_of(b,&view));
  CHECK(!memcmp(record[0],view.history[0],sizeof(record[0])));
  xe_xma_test_free(a);xe_xma_test_free(b);++cases;
 }
 CHECK(nonzero);
 printf("PASS: %u raw-XMA checkpoints, 192 future frames bit-identical with nonzero overlap, invalid-state nonmutation\n",cases);
 return 0;
}
