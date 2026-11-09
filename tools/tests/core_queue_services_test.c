#include <retro_atomic.h>
#include <rthreads/rthreads.h>
#include <rthreads/retro_eventcount.h>
#include <features/features_cpu.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"services line %u\n",(unsigned)__LINE__);exit(1);}}while(0)
void services_log_start(void(*)(void*,const char*,size_t),void*);
void services_log_write(const char*,size_t);void services_log_flush(void);void services_log_stop(void);int services_log_self(void);int services_log_quiet(void);void services_log_format(void);void services_log_level(int);
void*services_timer_new(void(*)(void*),void*,int64_t,int);void services_timer_disarm(void*);void services_timer_free(void*);void services_timer_stop(void);int services_timer_pause(void);void services_timer_resume(void);
struct capture {retro_atomic_int_t calls;retro_eventcount_t changed;};
static struct capture state;
static void wait_int(retro_atomic_int_t *value,int wanted,int64_t limit){int key;int64_t end=cpu_features_get_time_usec()+limit,left;
 while(retro_atomic_load_acquire_int(value)!=wanted){key=retro_eventcount_prepare_wait(&state.changed);left=end-cpu_features_get_time_usec();CHECK(left>0);if(retro_atomic_load_acquire_int(value)!=wanted)retro_eventcount_commit_wait_timeout(&state.changed,key,left);else retro_eventcount_cancel_wait(&state.changed);}}
struct writer {unsigned id,count;};
#if defined(_MSC_VER)
#define TEST_TLS __declspec(thread)
#else
#define TEST_TLS __thread
#endif
static TEST_TLS unsigned current_id, expected_index;
static TEST_TLS const char *borrowed;
static void sink(void*opaque,const char*bytes,size_t size){struct capture*s=(struct capture*)opaque;unsigned id,index,length;size_t j;
 CHECK(!services_log_self());
 if(size==4096){for(j=0;j<size;++j)CHECK(bytes[j]=='X');}
 else{CHECK(sscanf(bytes,"%u:%u:%u:",&id,&index,&length)==3);CHECK(bytes==borrowed && id==current_id && index==expected_index++ && length==size);for(j=32;j<size;++j)CHECK(bytes[j]==(char)('A'+id));}
 /* Recursion must not overwrite or re-enter this borrowed view. */
 services_log_write("nested",6);
 retro_atomic_fetch_add_int(&s->calls,1);
}
static void writer(void*opaque){struct writer*w=(struct writer*)opaque;unsigned i,n;char data[65537],prefix[32];current_id=w->id;borrowed=data;
 for(i=0;i<w->count;++i){n=i%17==0?65536:32+(i%511);memset(data,'A'+w->id,n);memset(data,32,32);sprintf(prefix,"%u:%u:%u:",w->id,i,n);memcpy(data,prefix,strlen(prefix));services_log_write(data,n);}}
static void timer_callback(void*opaque){(void)opaque;retro_atomic_fetch_add_int(&state.calls,1);retro_eventcount_notify(&state.changed);}
int main(int argc,char**argv){struct writer writers[4];sthread_t*threads[4];void*timers[1024];unsigned i;
 (void)argv;CHECK(argc==2);CHECK(retro_eventcount_init(&state.changed));services_log_start(sink,&state);
 CHECK(services_log_quiet()==0);CHECK(retro_atomic_load_acquire_int(&state.calls)==0);
 for(i=0;i<4;++i){writers[i].id=i;writers[i].count=1000;threads[i]=sthread_create(writer,writers+i);CHECK(threads[i]);}
 for(i=0;i<4;++i)sthread_join(threads[i]);services_log_flush();CHECK(retro_atomic_load_acquire_int(&state.calls)==4000);
 services_log_level(0);services_log_write("ignored warning",15);CHECK(retro_atomic_load_acquire_int(&state.calls)==4000);
 services_log_format();CHECK(retro_atomic_load_acquire_int(&state.calls)==4001);
 services_log_level(-1);services_log_format();CHECK(retro_atomic_load_acquire_int(&state.calls)==4001);services_log_stop();
 services_log_write("after shutdown",14);CHECK(retro_atomic_load_acquire_int(&state.calls)==4001);
 services_log_start(sink,&state);CHECK(services_log_quiet()==0);services_log_stop();
 retro_atomic_store_release_int(&state.calls,0);
 timers[0]=services_timer_new(timer_callback,NULL,3600000000LL,0);CHECK(timers[0]);CHECK(services_timer_pause());services_timer_resume();
 for(i=1;i<1024;++i){timers[i]=services_timer_new(timer_callback,NULL,3600000000LL,0);CHECK(timers[i]);}
 for(i=0;i<1024;++i)services_timer_disarm(timers[i]);CHECK(retro_atomic_load_acquire_int(&state.calls)==0);services_timer_stop();
 for(i=0;i<1024;++i){services_timer_disarm(timers[i]);services_timer_free(timers[i]);}
 timers[0]=services_timer_new(timer_callback,NULL,1000,0);CHECK(timers[0]);wait_int(&state.calls,1,5000000);services_timer_disarm(timers[0]);services_timer_free(timers[0]);services_timer_stop();
 retro_eventcount_free(&state.changed);puts("PASS: synchronous borrowed warnings, quiet macros, truncation/recursion/shutdown and timer saturation/cancel/shutdown/reinit");return 0;}
