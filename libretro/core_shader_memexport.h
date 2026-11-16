/* Bounded memexport dataflow. BSD license, MSVC C89.
 * Borrow final record bytes in place; no graph/payload copies or allocation.
 * Calls remain conservative as
 * in the existing analyzer: each call potentially writes the shader-wide mask.
 * Caller owns disjoint words, records, scratch and end byte through completion.
 */
#ifndef XE_CORE_SHADER_MEMEXPORT_H
#define XE_CORE_SHADER_MEMEXPORT_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <retro_inline.h>
#define XE_MEMEXPORT_CF_LIMIT 8192u
typedef struct xe_memexport_work {
 uint16_t queue[XE_MEMEXPORT_CF_LIMIT];
 uint8_t pending[XE_MEMEXPORT_CF_LIMIT/8];
} xe_memexport_work;
static INLINE void xe_memexport_cf(const uint32_t *words,size_t index,
 uint32_t *low,unsigned *opcode)
{
 const uint32_t *p=words+(index/2)*3;
 *low=(index&1)?(p[1]>>16)|(p[2]<<16):p[0];
 *opcode=((index&1)?p[2]>>16:p[1]&65535u)>>12;
}
static INLINE void xe_memexport_enqueue(xe_memexport_work *work,unsigned count,
 unsigned index,unsigned *tail)
{
 unsigned bit=1u<<(index&7);
 if(!(work->pending[index>>3]&bit)){
  work->pending[index>>3]|=(uint8_t)bit;
  work->queue[*tail%count]=(uint16_t)index;++*tail;
 }
}
static INLINE void xe_memexport_edge(unsigned char *records,size_t stride,
 size_t before,unsigned count,unsigned target,unsigned mask,uint8_t *end,
 xe_memexport_work *work,unsigned *tail)
{
 unsigned char *value;
 if(target==count){*end|=(uint8_t)mask;return;}
 value=records+target*stride+before;
 if((*value&mask)!=mask){*value|=(uint8_t)mask;xe_memexport_enqueue(work,count,target,tail);}
}
/* Reject malformed spans/branch targets before mutating final results. The
 * queue contains at most count distinct pending IDs. Each byte grows at most
 * eight times: <=9*count visits including initial writers, without recursion.
 * before/writes are byte offsets into each final record (no type punning).
 */
static INLINE int xe_shader_memexport_analyze(const uint32_t *words,size_t word_count,
 unsigned char *records,size_t stride,size_t before,size_t writes,unsigned count,
 uint8_t all_written,uint8_t *end,xe_memexport_work *work)
{
 unsigned i,opcode,mask,head=0,tail=0;uint32_t low;
 if(!end || !work || count>XE_MEMEXPORT_CF_LIMIT || (count&1) ||
  count/2>word_count/3 || !stride || before>=stride || writes>=stride || before==writes ||
  count>(size_t)-1/stride || (count && (!words || !records)))return 0;
 for(i=0;i<count;++i){
  xe_memexport_cf(words,i,&low,&opcode);
  if((opcode==7 || opcode==8 || opcode==9 || opcode==11) && (low&8191u)>count)return 0;
 }
 memset(work->pending,0,(count+7)/8);*end=0;
 for(i=0;i<count;++i){
  records[i*stride+before]=0;xe_memexport_cf(words,i,&low,&opcode);
  if(records[i*stride+writes] || (opcode==9 && all_written))xe_memexport_enqueue(work,count,i,&tail);
 }
 while(head!=tail){
  i=work->queue[head%count];++head;
  work->pending[i>>3]&=(uint8_t)~(1u<<(i&7));
  xe_memexport_cf(words,i,&low,&opcode);
  mask=records[i*stride+before]|records[i*stride+writes];
  if(opcode==9)mask|=all_written;
  if(!mask || opcode==12 || opcode==10)continue;
  if(opcode==2 || opcode==4 || opcode==6 || opcode==14)*end|=(uint8_t)mask;
  if(opcode==2)continue;
  if(opcode==7 || opcode==8 || opcode==9 || opcode==11)
   xe_memexport_edge(records,stride,before,count,low&8191u,mask,end,work,&tail);
  if(opcode!=11 || !(low&8192u))
   xe_memexport_edge(records,stride,before,count,i+1,mask,end,work,&tail);
 }
 return 1;
}
/* Isolate the bounded 17-KiB stack workspace from ordinary no-export analysis.
 * Do not inline this wrapper into Shader::AnalyzeUcode: most shaders need no
 * memexport workspace and must not pay its stack-frame/probe cost. */
#if defined(_MSC_VER)
#define XE_MEMEXPORT_NOINLINE __declspec(noinline)
#else
#define XE_MEMEXPORT_NOINLINE __attribute__((noinline))
#endif
static XE_MEMEXPORT_NOINLINE int xe_shader_memexport_run(const uint32_t *words,
 size_t word_count,unsigned char *records,size_t stride,size_t before,size_t writes,
 unsigned count,uint8_t all_written,uint8_t *end)
{
 xe_memexport_work work;
 return xe_shader_memexport_analyze(words,word_count,records,stride,before,writes,
  count,all_written,end,&work);
}
#undef XE_MEMEXPORT_NOINLINE
#endif
