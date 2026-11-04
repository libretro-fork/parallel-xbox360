/* Offline native XMA ring publication and consumer cadence. No renderer/game. */
#include "xenia/memory.h"
#include "xenia/apu/xma_context_new.h"
#include "xenia/apu/xma_helpers.h"
#include "xenia/base/logging.h"
#include "xenia/base/profiling.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
namespace xe { namespace apu {
struct XmaContextStreamTest {
  static unsigned pending(XmaContextNew *context) { return context->current_frame_remaining_subframes_; }
  static int free_blocks(XmaContextNew *context) { return context->remaining_subframe_blocks_in_output_buffer_; }
};
}}
static unsigned char input[131072];
static unsigned char reference[2097152];
static unsigned reference_bytes;
static int run(xe::Memory *memory,unsigned input_bytes,unsigned blocks,unsigned sdc,int record,unsigned chunk) {
  uint32_t context_address,input_address,output_address,physical_input,physical_output;
  unsigned capacity,iteration,read,write,free_before,written,published,available,take,first,total=0;
  unsigned next_packet,packet_count,portion,starvations=0;
  const unsigned char *pcm;
  unsigned char *context_bytes;
  int result=1;
  context_address=memory->SystemHeapAlloc(4096);
  input_address=memory->SystemHeapAlloc(sizeof(input),4096,xe::kSystemHeapPhysical);
  output_address=memory->SystemHeapAlloc(8192,4096,xe::kSystemHeapPhysical);
  if (!context_address || !input_address || !output_address) return 0;
  context_bytes=memory->TranslateVirtual(context_address);
  memset(context_bytes,0,64);
  physical_input=memory->GetPhysicalAddress(input_address);
  physical_output=memory->GetPhysicalAddress(output_address);
  memcpy(memory->TranslatePhysical(physical_input),input,input_bytes);
  capacity=blocks*256;
  {
    xe::apu::XmaContextNew decoder;
    xe::apu::XMA_CONTEXT_DATA state(context_bytes);
    if (decoder.Setup(0,memory,context_address)) return 0;
    decoder.set_is_allocated(true);
    state.input_buffer_0_ptr=physical_input;
    packet_count=input_bytes/2048;
    next_packet=chunk && chunk<packet_count?chunk:packet_count;
    state.input_buffer_0_packet_count=next_packet;
    state.input_buffer_0_valid=1;
    state.input_buffer_read_offset=xe::apu::xma::GetPacketFrameOffset(input);
    state.output_buffer_ptr=physical_output;
    state.output_buffer_block_count=blocks;
    state.subframe_decode_count=sdc;
    state.is_stereo=1;
    state.sample_rate=3;
    for (iteration=0;iteration<10000;++iteration) {
      xe::apu::XMA_CONTEXT_DATA before(context_bytes);
      if (!iteration) before=state;
      if (chunk && !before.input_buffer_0_valid && next_packet<packet_count) {
        portion=packet_count-next_packet;
        if (portion>chunk) portion=chunk;
        before.input_buffer_0_ptr=physical_input+next_packet*2048;
        before.input_buffer_0_packet_count=portion;
        before.input_buffer_0_valid=1;
        next_packet+=portion;
      }
      if (chunk && !before.input_buffer_1_valid && next_packet<packet_count) {
        portion=packet_count-next_packet;
        if (portion>chunk) portion=chunk;
        before.input_buffer_1_ptr=physical_input+next_packet*2048;
        before.input_buffer_1_packet_count=portion;
        before.input_buffer_1_valid=1;
        next_packet+=portion;
      }
      before.output_buffer_valid=1;
      before.Store(context_bytes);
      read=before.output_buffer_read_offset*256;
      write=before.output_buffer_write_offset*256;
      free_before=read==write?blocks:((read+capacity-write)%capacity)/256;
      decoder.Enable();
      if (!decoder.Work()) { result=0; break; }
      state=xe::apu::XMA_CONTEXT_DATA(context_bytes);
      written=(free_before-(unsigned)xe::apu::XmaContextStreamTest::free_blocks(&decoder))*256;
      published=(state.output_buffer_write_offset*256+capacity-write)%capacity;
      if (published!=written%capacity) {
        fprintf(stderr,"FAIL: blocks=%u sdc=%u step=%u decoded=%u published=%u input_valid=%u pending=%u\n",
          blocks,sdc,iteration,written,published,(unsigned)before.IsAnyInputBufferValid(),
          xe::apu::XmaContextStreamTest::pending(&decoder));
        result=0;break;
      }
      available=(state.output_buffer_write_offset*256+capacity-read)%capacity;
      if (!available && written) available=capacity;
      if (!available && !state.IsAnyInputBufferValid() &&
          !xe::apu::XmaContextStreamTest::pending(&decoder)) break;
      take=available<1024?available:1024; /* 256 stereo int16 frames. */
      first=take<capacity-read?take:capacity-read;
      pcm=memory->TranslatePhysical(physical_output);
      if (total+take>sizeof(reference)) { result=0; break; }
      if (record) {
        memcpy(reference+total,pcm+read,first);
        memcpy(reference+total+first,pcm,take-first);
      } else if (total+take>reference_bytes ||
          memcmp(reference+total,pcm+read,first) ||
          memcmp(reference+total+first,pcm,take-first)) {
        fprintf(stderr,"FAIL: nonconsecutive PCM ring=%u sdc=%u byte=%u\n",blocks,sdc,total);
        result=0;break;
      }
      total+=take;
      state.output_buffer_read_offset=((read+take)%capacity)/256;
      state.Store(context_bytes);
      /* The guest may temporarily exhaust compressed input while decoded
       * PCM is still queued. An empty decoder pass must not consume it. */
      if (iteration%6==2 && state.IsAnyInputBufferValid() &&
          !xe::apu::XmaContextStreamTest::pending(&decoder) && available>take) {
        xe::apu::XMA_CONTEXT_DATA paused(state), after(state);
        paused.input_buffer_0_valid=0;
        paused.input_buffer_1_valid=0;
        paused.output_buffer_valid=1;
        paused.Store(context_bytes);
        decoder.Enable(); decoder.Work();
        after=xe::apu::XMA_CONTEXT_DATA(context_bytes);
        if (after.output_buffer_write_offset!=state.output_buffer_write_offset) {
          fprintf(stderr,"FAIL: starvation discarded unread PCM at quantum %u\n",iteration);
          result=0;break;
        }
        after.input_buffer_0_valid=state.input_buffer_0_valid;
        after.input_buffer_1_valid=state.input_buffer_1_valid;
        after.Store(context_bytes);
        ++starvations;
      }
    }
    if (record) reference_bytes=total;
    else if (total!=reference_bytes) result=0;
    if (iteration==10000 || total<48000*4) result=0;
    printf("ring=%u sdc=%u chunk=%u consumed=%u starvation_checks=%u result=%s\n",blocks,sdc,chunk,total,starvations,result?"PASS":"FAIL");
  }
  memory->SystemHeapFree(output_address);
  memory->SystemHeapFree(input_address);
  memory->SystemHeapFree(context_address);
  return result;
}
int main(void) {
  const char *path=getenv("XMA_INPUT_RAW");
  FILE *file;
  unsigned bytes,blocks,sdc,chunk,i;
  unsigned nonzero=0;
  int ok=1;
  xe::Memory memory;
  if (!path) return 2;
  file=fopen(path,"rb"); if (!file) return 2;
  bytes=(unsigned)fread(input,1,sizeof(input),file); fclose(file);
  if (!bytes || bytes%2048) return 2;
  xe::InitializeLogging("xma_stream_test"); xe::Profiler::Initialize();
  if (!memory.Initialize()) return 2;
  for (blocks=8;blocks<=24;blocks+=8) for(sdc=1;sdc<=8;sdc*=2)
    if (!run(&memory,bytes,blocks,sdc,blocks==8 && sdc==1,0)) ok=0;
  for (chunk=1;chunk<=7;chunk+=2) for (blocks=8;blocks<=24;blocks+=8)
    if (!run(&memory,bytes,blocks,4,0,chunk)) ok=0;
  for (i=0;i<reference_bytes;++i) if (reference[i]) ++nonzero;
  if (nonzero<1024) ok=0;
  path=getenv("XMA_OUTPUT_REFERENCE");
  if (path && ok) {
    file=fopen(path,"wb");
    if (!file) return 2;
    if (fwrite(reference,1,reference_bytes,file)!=reference_bytes) ok=0;
    if (fclose(file)) ok=0;
  }
  printf("PCM bytes=%u nonzero=%u exact across 24 consumption/refill configurations\n",reference_bytes,nonzero);
  return ok?0:1;
}
