/* Typed move/lifetime interoperability only. Test decisions live in C89. */
#include "draw_contract_bridge.h"
#include "libretro/core_draw_stats.h"
#include "xenia/gpu/pipeline_creation_queue.h"
struct DrawRequest {
 size_t id; unsigned* moves;
 DrawRequest(size_t value,unsigned* count) noexcept:id(value),moves(count){}
 DrawRequest(DrawRequest&& other) noexcept:id(other.id),moves(other.moves){++*moves;other.id=0;}
 DrawRequest(const DrawRequest&)=delete;
};
struct DrawQueue {
 draw_callbacks callbacks;
 xe::gpu::PipelineCreationQueue<DrawRequest,uintptr_t,unsigned> queue;
};
extern "C" void* draw_queue_new(const draw_callbacks* callbacks){
 auto* value=new DrawQueue;value->callbacks=*callbacks;
 value->queue.Initialize("Draw contract",&value->callbacks,
  [](void* p,const DrawRequest& request,unsigned*){auto* c=static_cast<draw_callbacks*>(p);return c->create(c->owner,request.id);},
  [](void* p,const DrawRequest& request,uintptr_t result){auto* c=static_cast<draw_callbacks*>(p);c->store(c->owner,request.id,result);},nullptr);
 return value;
}
extern "C" void draw_queue_threads(void* value,size_t count){static_cast<DrawQueue*>(value)->queue.SetThreadCount(count);}
extern "C" int draw_queue_try(void* value,size_t* id,unsigned* moves){
 DrawRequest request(*id,moves);bool accepted=static_cast<DrawQueue*>(value)->queue.TryPush(std::move(request));*id=request.id;return accepted;
}
extern "C" void draw_queue_free(void* value){delete static_cast<DrawQueue*>(value);}

extern "C" int draw_scope_unwind(void){
 unsigned before=xe_draw_stats_role();
 try { XE_DRAW_SCOPE(XE_DRAW_INITIALIZATION); throw 1; } catch(int) {}
 return xe_draw_stats_role()==before;
}
