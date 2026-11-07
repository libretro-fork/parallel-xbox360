/* Native exception/lifetime ABI only; verification lives in C89. */
#include "core_gpu_tasks.h"
static void Throw(void*,const void*,size_t) { throw 17; }
extern "C" xe_gpu_task_fn xe_test_tasks_throw_fn() { return Throw; }
extern "C" int xe_test_tasks_take_throw(xe_gpu_tasks* q) {
  xe_gpu_task_view view;
  if(!xe_gpu_tasks_take(q,&view)) return 0;
  try { view.task->run(view.task->context,view.task->data,view.task->size); }
  catch(int value) { return value==17 && xe_gpu_tasks_finish(q,&view); }
  return 0;
}
extern "C" int xe_test_tasks_inline_throw() {
  unsigned a=1,b=2;
  unsigned char payload[8];
  xe_gpu_task_fn volatile fn=xe_test_tasks_throw_fn();
  if(!xe_gpu_tasks_copy_parts(payload,sizeof(payload),&a,sizeof(a),&b,sizeof(b))) return 0;
  try { fn(nullptr,payload,sizeof(payload)); }
  catch(int value) { return value==17; }
  return 0;
}
