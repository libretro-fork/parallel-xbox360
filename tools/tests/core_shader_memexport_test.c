/* Independent per-writer graph reachability vs borrowed-byte worklist.
 * Includes abandoned pending branches and the last-CF boundary. MSVC C89. */
#include <stdio.h>
#include <stdlib.h>
#include "libretro/core_shader_memexport.h"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"memexport line %u\n",(unsigned)__LINE__);exit(1);}}while(0)
typedef struct record { uint8_t guard,before,writes,tail; } record;
static record values[XE_MEMEXPORT_CF_LIMIT];
static uint32_t code[XE_MEMEXPORT_CF_LIMIT/2*3];
static xe_memexport_work scratch;
static unsigned opcode[XE_MEMEXPORT_CF_LIMIT],target[XE_MEMEXPORT_CF_LIMIT],unconditional[XE_MEMEXPORT_CF_LIMIT];
static uint8_t expected[XE_MEMEXPORT_CF_LIMIT],visited[XE_MEMEXPORT_CF_LIMIT];
static unsigned stack[XE_MEMEXPORT_CF_LIMIT*2+2],cases;
static uint32_t seed=UINT32_C(0x74831b17);
static unsigned random_word(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
static void pack(unsigned count)
{
 unsigned i;uint32_t low,*p;
 memset(code,0,sizeof(code));
 for(i=0;i<count;++i){
  low=target[i]|(unconditional[i]?8192u:0);p=code+(i/2)*3;
  if(i&1){p[1]|=low<<16;p[2]=(low>>16)|(opcode[i]<<28);}
  else{p[0]=low;p[1]=opcode[i]<<12;}
 }
}
/* Separate per-source DFS on explicit test graph. Nodes are marked at enqueue
 * and paths end at alloc/return. This oracle neither decodes packed CF nor
 * shares the candidate queue, edge propagation or combined fixed point. */
static uint8_t oracle(unsigned count,uint8_t all)
{
 unsigned source,index,n,j,next[2],length,mask;uint8_t end=0;
 memset(expected,0,sizeof(expected));
 for(source=0;source<count;++source){
  mask=values[source].writes;if(opcode[source]==9)mask|=all;if(!mask)continue;
  memset(visited,0,count);length=0;index=source;
  for(;;){
   n=0;
   if(opcode[index]!=12 && opcode[index]!=10){
    if(opcode[index]==2 || opcode[index]==4 || opcode[index]==6 || opcode[index]==14)end|=(uint8_t)mask;
    if(opcode[index]!=2){
     if(opcode[index]==7 || opcode[index]==8 || opcode[index]==9 || opcode[index]==11)next[n++]=target[index];
     if(opcode[index]!=11 || !unconditional[index])next[n++]=index+1;
    }
   }
   for(j=0;j<n;++j){
    if(next[j]==count){end|=(uint8_t)mask;continue;}
    CHECK(next[j]<count);expected[next[j]]|=(uint8_t)mask;
    if(!visited[next[j]]){visited[next[j]]=1;CHECK(length<count);stack[length++]=next[j];}
   }
   if(!length)break;
   index=stack[--length];
  }
 }
 return end;
}
/* Exact legacy traversal scheduling for the focused regression: stop the
 * entire walk on a visited node, retaining the pending vector between writers.
 * This intentionally preserves the defect; never use it as the oracle. */
static uint8_t legacy_before[8];
static uint8_t legacy_regression(unsigned count)
{
 unsigned source,index,length=0,first,mask,j,n,next[2];uint8_t end=0;
 memset(legacy_before,0,sizeof(legacy_before));
 for(source=0;source<count;++source){
  mask=values[source].writes;if(!mask)continue;first=1;stack[length++]=source;
  while(length){
   index=stack[--length];CHECK(index<count);
   if((legacy_before[index]&mask)==mask)break;
   if(!first)legacy_before[index]|=(uint8_t)mask;
   first=0;n=0;
   if(opcode[index]==2)end|=(uint8_t)mask;
   else{
    if(opcode[index]==11)next[n++]=target[index];
    if(opcode[index]!=11 || !unconditional[index])next[n++]=index+1;
   }
   for(j=0;j<n;++j){CHECK(length<count*2);stack[length++]=next[j];}
  }
 }
 return end;
}
static void check(unsigned count)
{
 unsigned i;uint8_t end=0,all=0,wanted;
 for(i=0;i<count;++i){all|=values[i].writes;values[i].guard=0xa5;values[i].tail=0x5a;values[i].before=0xee;}
 pack(count);wanted=oracle(count,all);
 CHECK(xe_shader_memexport_analyze(code,count/2*3,(unsigned char*)values,sizeof(record),offsetof(record,before),offsetof(record,writes),count,all,&end,&scratch));
 CHECK(end==wanted);
 CHECK(xe_shader_memexport_run(code,count/2*3,(unsigned char*)values,sizeof(record),offsetof(record,before),offsetof(record,writes),count,all,&end));
 CHECK(end==wanted);
 for(i=0;i<count;++i){CHECK(values[i].before==expected[i]);CHECK(values[i].guard==0xa5 && values[i].tail==0x5a);}
 ++cases;
}
int main(void)
{
 unsigned i,n,iteration;uint8_t end=0x7b;record saved;
 /* First writer reaches a cycle while the end branch remains pending. A second
  * writer must not consume abandoned work carrying the wrong writer's mask. */
 memset(values,0,sizeof(values));memset(opcode,0,sizeof(opcode));memset(target,0,sizeof(target));memset(unconditional,0,sizeof(unconditional));
 opcode[0]=1;values[0].writes=1;opcode[1]=11;target[1]=4;opcode[3]=11;target[3]=2;unconditional[3]=1;
 opcode[4]=2;opcode[5]=2;values[5].writes=2;check(6);CHECK(values[4].before==1);
 CHECK(legacy_regression(6)==2 && legacy_before[4]==2);
 /* Fallthrough from final CF is the end, never record[count]. */
 memset(values,0,sizeof(values));memset(opcode,0,sizeof(opcode));values[0].writes=255;opcode[0]=1;check(2);
 for(iteration=0;iteration<4000;++iteration){
  n=2+2*(random_word()%64);
  for(i=0;i<n;++i){opcode[i]=random_word()%16;target[i]=random_word()%(n+1);unconditional[i]=random_word()&1;values[i].writes=(opcode[i]>=1 && opcode[i]<=6)?(uint8_t)random_word():0;}
  check(n);
 }
 /* Full address domain, forward chain and backward strongly-connected path. */
 n=XE_MEMEXPORT_CF_LIMIT;memset(values,0,sizeof(values));memset(opcode,0,sizeof(opcode));memset(unconditional,0,sizeof(unconditional));
 values[0].writes=0x81;opcode[0]=1;opcode[n-1]=11;target[n-1]=0;unconditional[n-1]=1;check(n);
 saved=values[0];target[1]=3;opcode[1]=11;pack(2);
 CHECK(!xe_shader_memexport_analyze(code,3,(unsigned char*)values,sizeof(record),1,2,2,1,&end,&scratch));
 CHECK(!memcmp(&saved,&values[0],sizeof(saved)) && end==0x7b);
 CHECK(!xe_shader_memexport_analyze(code,3,(unsigned char*)values,sizeof(record),1,1,2,1,&end,&scratch));
 CHECK(!xe_shader_memexport_analyze(code,3,(unsigned char*)values,sizeof(record),1,2,3,1,&end,&scratch));
 CHECK(!xe_shader_memexport_analyze(code,3,(unsigned char*)values,(size_t)-1,1,2,2,1,&end,&scratch));
 printf("PASS: %u memexport graphs, complete address boundary, borrowed guards and malformed input rejection; dataflow component\n",cases);return 0;
}
