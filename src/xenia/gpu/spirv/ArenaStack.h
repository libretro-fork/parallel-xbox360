/* Typed lifetime bridge only. Paging, growth and retirement live in C89. */
#ifndef XENIA_GPU_SPIRV_ARENA_STACK_H_
#define XENIA_GPU_SPIRV_ARENA_STACK_H_
#include "spvIR.h"
#include <type_traits>
namespace spv {
template <class T> class ArenaStack final {
 public:
    static_assert(std::is_trivially_destructible_v<T>);
    static_assert(std::is_nothrow_copy_constructible_v<T>);
    static_assert(alignof(T) <= 8);
    ArenaStack(Module& owner, unsigned kind) noexcept : owner(owner), kind(kind) {}
    // The module owns storage and retires all trivial frames before IR cleanup.
    ~ArenaStack() = default;
    ArenaStack(const ArenaStack&) = delete;
    ArenaStack& operator=(const ArenaStack&) = delete;
    bool empty() const { return owner.stackTop(kind) == nullptr; }
    void push(const T& value) { ::new (owner.stackPush(kind, sizeof(T))) T(value); }
    T& top() {
        T* value = static_cast<T*>(owner.stackTop(kind));
        if (!value) throw std::bad_alloc();
        return *value;
    }
    const T& top() const {
        const T* value = static_cast<const T*>(owner.stackTop(kind));
        if (!value) throw std::bad_alloc();
        return *value;
    }
    void pop() {
        top().~T();
        const bool popped = owner.stackPop(kind);
        assert(popped);
        (void)popped;
    }
    void clear() noexcept { owner.stackClear(kind); }
 private:
    Module& owner;
    unsigned kind;
};
}
#endif
