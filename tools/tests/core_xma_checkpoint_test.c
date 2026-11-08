/* Native raw XMA checkpoint and PCM continuation. MSVC C89. */
#include <formats/rwma.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)) {fprintf(stderr,"xma checkpoint line %u\n",(unsigned)__LINE__);exit(1);}} while(0)
static float history[2][256],record[2][256],reference[8][2][512],before_pcm[2][256];
static unsigned char encoded[80];
static void put_bits(unsigned char *p,unsigned *position,unsigned value,unsigned count) {
 unsigned i;
 for(i=count;i;--i) {if((value>>(i-1))&1) p[*position/8]|=(unsigned char)(1u<<(7-*position%8));++*position;}
}
static unsigned packet(unsigned channels,unsigned padding) {
 unsigned bits=channels==1?31:36,pos=8+padding;
 unsigned end=(8+padding+bits+7)/8;
 memset(encoded,0,sizeof(encoded));
 encoded[0]=(unsigned char)((padding<<5)|((end*8-8-padding-bits)<<2));
 put_bits(encoded,&pos,bits,15);put_bits(encoded,&pos,1,1);put_bits(encoded,&pos,0,1);
 if(channels==2) put_bits(encoded,&pos,0,1);
 put_bits(encoded,&pos,0,8);put_bits(encoded,&pos,0,1);
 put_bits(encoded,&pos,0,1);put_bits(encoded,&pos,0,1);
 if(channels==2) {put_bits(encoded,&pos,0,1);put_bits(encoded,&pos,2,2);}
 put_bits(encoded,&pos,0,channels);put_bits(encoded,&pos,0,2);
 CHECK(pos==8+padding+bits);return end;
}
static void unchanged(rwma_t *ctx,const rwma_xma_view *invalid) {
 rwma_xma_view before,after;
 unsigned c;
 CHECK(!rwma_xma_capture(ctx,&before));
 for(c=0;c<2;++c) if(before.history_samples[c])
  memcpy(before_pcm[c],before.history[c],before.history_samples[c]*sizeof(float));
 CHECK(rwma_xma_restore(ctx,invalid)!=RWMA_OK);
 CHECK(!rwma_xma_capture(ctx,&after));
 CHECK(!memcmp(&before.state,&after.state,sizeof(before.state)));
 for(c=0;c<2;++c) {
  CHECK(before.history_samples[c]==after.history_samples[c]);
  if(before.history_samples[c]) CHECK(!memcmp(before_pcm[c],after.history[c],before.history_samples[c]*sizeof(float)));
 }
}
int main(void) {
 const unsigned rates[4]={24000,32000,44100,48000};
 rwma_t *a,*b;
 rwma_config config;
 rwma_xma_view seed,saved,view,bad;
 rwma_frame frame;
 unsigned rate,channels,previous,c,i,n,bytes,nonzero=0,cases=0;
 uint32_t bits;
 CHECK(rwma_xma_capture(NULL,&view)!=RWMA_OK);
 CHECK(rwma_xma_restore(NULL,&view)!=RWMA_OK);
 memset(&config,0,sizeof(config));config.codec=RWMA_XMA_RAW;
 for(rate=0;rate<4;++rate) for(channels=1;channels<=2;++channels) for(previous=128;previous<=512;previous*=2) {
  config.sample_rate=rates[rate];config.channels=channels;
  CHECK(!rwma_create(&a,&config) && !rwma_create(&b,&config));
  CHECK(!rwma_xma_capture(a,&seed));seed.state.pending=1;
  for(c=0;c<channels;++c) {
   for(i=0;i<256;++i) history[c][i]=(float)((int)(i%37)-18)*(float)(c+1)*0.001953125f;
   seed.history[c]=history[c];seed.history_samples[c]=256;seed.state.previous[c]=previous;
  }
  CHECK(!rwma_xma_restore(a,&seed) && !rwma_xma_capture(a,&saved));
  CHECK(saved.history[0]!=history[0]);
  for(c=0;c<channels;++c) {memcpy(record[c],saved.history[c],sizeof(record[c]));saved.history[c]=record[c];}
  CHECK(!rwma_xma_restore(b,&saved));
  bad=saved;bad.state.version=0;unchanged(b,&bad);
  bad=saved;bad.state.sample_rate=1;unchanged(b,&bad);
  bad=saved;bad.state.channels=3;unchanged(b,&bad);
  bad=saved;bad.state.previous[0]=192;unchanged(b,&bad);
  bad=saved;bad.state.pending=2;unchanged(b,&bad);
  bad=saved;bad.state.failed=1;unchanged(b,&bad);
  bad=saved;bad.state.drained=2;unchanged(b,&bad);
  bad=saved;bad.history[0]=NULL;unchanged(b,&bad);
  bad=saved;bad.history_samples[0]--;unchanged(b,&bad);
  bad=saved;bad.history[0]=(const float *)(UINTPTR_MAX-16);unchanged(b,&bad);
  if(channels==2) {
   bad=saved;bad.history_samples[1]--;unchanged(b,&bad);
   bad=saved;bad.state.previous[1]=192;unchanged(b,&bad);
  } else {
   bad=saved;bad.state.previous[1]=128;unchanged(b,&bad);
   bad=saved;bad.history[1]=record[0];unchanged(b,&bad);
   bad=saved;bad.history_samples[1]=256;unchanged(b,&bad);
  }
  bits=UINT32_C(0x7fc12345);memcpy(&record[channels-1][255],&bits,4);
  unchanged(b,&saved);
  memcpy(&record[channels-1][255],&history[channels-1][255],4);
  for(n=0;n<8;++n) {
   bytes=packet(channels,n);
   CHECK(!rwma_decode_xma(a,encoded+1,bytes-1,n,channels==1?31:36,&frame) && frame.samples==512);
   for(c=0;c<channels;++c) {
    memcpy(reference[n][c],frame.planes[c],sizeof(reference[n][c]));
    for(i=0;i<512;++i) if(reference[n][c][i]!=0.0f) ++nonzero;
   }
  }
  CHECK(!rwma_xma_restore(b,&saved) && !rwma_xma_capture(b,&view));
  CHECK(!memcmp(&saved.state,&view.state,sizeof(saved.state)));
  for(c=0;c<channels;++c) CHECK(!memcmp(record[c],view.history[c],sizeof(record[c])));
  for(n=0;n<8;++n) {
   bytes=packet(channels,n);
   CHECK(!rwma_decode_xma(b,encoded+1,bytes-1,n,channels==1?31:36,&frame) && frame.samples==512);
   for(c=0;c<channels;++c) CHECK(!memcmp(reference[n][c],frame.planes[c],sizeof(reference[n][c])));
  }
  bits=UINT32_C(0x80000000);memcpy(&record[0][0],&bits,4);
  CHECK(!rwma_xma_restore(b,&saved) && !rwma_xma_capture(b,&view));
  CHECK(!memcmp(record[0],view.history[0],sizeof(record[0])));
  rwma_free(a);rwma_free(b);++cases;
 }
 CHECK(nonzero);
 printf("PASS: %u native XMA checkpoints, 192 future frames bit-identical with nonzero overlap and rejected-state nonmutation\n",cases);
 return 0;
}
