#include "xenia/base/mapped_memory.h"
#include <cstdlib>
#include <cstdio>
#define CHECK(x) do { if (!(x)) std::abort(); } while(0)
int main() {
  unsigned char bytes[64] = {};
  xe::MappedMemory owner(bytes,sizeof(bytes));
  for(size_t offset=0;offset<72;++offset) for(size_t length=0;length<72;++length) {
    auto view=owner.Slice(offset,length);
    const bool valid=offset<=sizeof(bytes) && length<=sizeof(bytes)-offset;
    CHECK(bool(view)==valid);
    if(view) {
      CHECK(view->data()==bytes+offset && view->size()==length);
      if(length) { view->data()[0]=0xAD; CHECK(bytes[offset]==0xAD); }
    }
  }
  CHECK(!owner.Slice(SIZE_MAX,1));
  CHECK(!owner.Slice(1,SIZE_MAX));
  xe::MappedMemory empty(nullptr,0);
  auto empty_view=empty.Slice(0,0);
  CHECK(empty_view && !empty_view->data() && !empty_view->size());
  CHECK(!empty.Slice(0,1) && !empty.Slice(1,0));
  auto view=owner.Slice(7,5);
  CHECK(view && !view->Slice(4,2) && view->Slice(5,0));
  std::puts("PASS: production MappedMemory slices borrow exact bounded views");
}
