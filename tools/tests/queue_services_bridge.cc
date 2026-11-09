/* Typed host-service ABI only; test policy and synchronization are C89. */
#include "xenia/base/logging.h"
#include "xenia/base/cvar.h"
#include "xenia/base/threading_timer_queue.h"
#include <string_view>
DECLARE_int32(log_level);
static void (*write_callback)(void*, const char*, size_t);
static void* write_context;
static void write_message(int level, const char* bytes, size_t size, void*) {
 if(level != int(xe::LogLevel::Warning) && level != int(xe::LogLevel::Error)) std::abort();
 write_callback(write_context,bytes,size);
}
extern "C" void services_log_start(void(*write)(void*,const char*,size_t),void* context){
 cvars::log_level=3;write_callback=write;write_context=context;
 xe::SetLogCallback(write_message,nullptr);xe::InitializeLogging("queue-test");}
extern "C" void services_log_write(const char* bytes,size_t size){xe::logging::AppendLogLine(xe::LogLevel::Warning,0,std::string_view(bytes,size));}
extern "C" int services_log_quiet(){
 int count=0;
 XELOGI("{}",++count);XELOGD("{}",++count);XELOGCPU("{}",++count);
 XELOGAPU("{}",++count);XELOGGPU("{}",++count);XELOGKERNEL("{}",++count);XELOGFS("{}",++count);
 xe::logging::AppendLogLine(xe::LogLevel::Info,0,"suppressed");
 return count;
}
extern "C" void services_log_level(int level){cvars::log_level=level;}
extern "C" void services_log_format(){XELOGE("{}",std::string(12000,'X'));}
extern "C" void services_log_flush(){xe::FlushLog();}
extern "C" void services_log_stop(){xe::ShutdownLogging();}
extern "C" int services_log_self(){return xe::IsLogWriterThread();}
extern "C" void* services_timer_new(void(*callback)(void*),void* opaque,int64_t delay_us,int recurring){
 using namespace xe::threading;auto due=TimerQueueWaitItem::clock::now()+std::chrono::microseconds(delay_us);
 auto weak=recurring?QueueTimerRecurring(callback,opaque,due,std::chrono::microseconds(delay_us)):QueueTimerOnce(callback,opaque,due);
 return new std::shared_ptr<TimerQueueWaitItem>(weak.lock());}
extern "C" void services_timer_disarm(void* p){auto* item=static_cast<std::shared_ptr<xe::threading::TimerQueueWaitItem>*>(p);if(*item)(*item)->Disarm();}
extern "C" void services_timer_free(void* p){delete static_cast<std::shared_ptr<xe::threading::TimerQueueWaitItem>*>(p);}
extern "C" void services_timer_stop(){xe::threading::ShutdownTimerQueue();}
extern "C" int services_timer_pause(){return xe::threading::PauseTimerQueueForState(2000000);}
extern "C" void services_timer_resume(){xe::threading::ResumeTimerQueueFromState();}
