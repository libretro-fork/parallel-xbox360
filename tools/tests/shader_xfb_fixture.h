/* Test-only Xfb annotations; guest instructions and IDs remain unchanged. C89. */
#ifndef XE_TEST_SHADER_XFB_FIXTURE_H
#define XE_TEST_SHADER_XFB_FIXTURE_H
static uint32_t shader_test_xfb_pointers[65536];
static size_t shader_test_xfb(const uint32_t *words,size_t count, uint32_t *out_words,size_t capacity) {
 size_t at,n,out=5;uint32_t vertex_type=0,variable=0,entry=0,id;
 unsigned op,mode=0,decor=0;
 CHECK(count>=5 && count<=65536 && capacity>=count+23 && words[0]==0x07230203 && words[3]<65536);
 memset(shader_test_xfb_pointers,0,sizeof(shader_test_xfb_pointers));
 for(at=5;at<count;at+=n) {
  n=words[at]>>16;op=words[at]&65535;CHECK(n && n<=count-at);
  if(op==72 && n==5 && words[at+2]==0 && words[at+3]==11 && words[at+4]==0)vertex_type=words[at+1];
  if(op==32 && n==4 && words[at+2]==3){id=words[at+1];CHECK(id<65536);shader_test_xfb_pointers[id]=words[at+3];}
  if(op==15){CHECK(!entry && n>=4 && words[at+1]==0);entry=words[at+2];}
 }
 CHECK(vertex_type && entry);
 for(at=5;at<count;at+=n) {
  n=words[at]>>16;op=words[at]&65535;
  if(op==59 && n>=4 && words[at+3]==3 && words[at+1]<65536 && shader_test_xfb_pointers[words[at+1]]==vertex_type) {
   CHECK(!variable);variable=words[at+2];
  }
 }
 CHECK(variable);memcpy(out_words,words,20);
 out_words[out++]=(2u<<16)|17;out_words[out++]=53; /* TransformFeedback capability. */
 for(at=5;at<count;at+=n) {
  n=words[at]>>16;op=words[at]&65535;
  if(!mode && op!=17 && op!=10 && op!=11 && op!=14 && op!=15 && op!=16) {
   out_words[out++]=(3u<<16)|16;out_words[out++]=entry;out_words[out++]=11;mode=1; /* Xfb. */
  }
  if(!decor && op>=19 && op<=39) {
   out_words[out++]=(4u<<16)|71;out_words[out++]=variable;out_words[out++]=36;out_words[out++]=0;
   out_words[out++]=(4u<<16)|71;out_words[out++]=variable;out_words[out++]=37;out_words[out++]=16;
   out_words[out++]=(5u<<16)|72;out_words[out++]=vertex_type;out_words[out++]=0;out_words[out++]=35;out_words[out++]=0;
   decor=1;
  }
  CHECK(out+n<=capacity);memcpy(out_words+out,words+at,n*4);out+=n;
 }
 CHECK(mode && decor);return out;
}
#endif
