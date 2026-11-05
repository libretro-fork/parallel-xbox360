//
// Copyright (C) 2016 Google, Inc.
//
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
//
//    Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
//
//    Redistributions in binary form must reproduce the above
//    copyright notice, this list of conditions and the following
//    disclaimer in the documentation and/or other materials provided
//    with the distribution.
//
//    Neither the name of 3Dlabs Inc. Ltd. nor the names of its
//    contributors may be used to endorse or promote products derived
//    from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
// FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
// COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
// INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
// BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
// LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
// ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

// The SPIR-V spec requires code blocks to appear in an order satisfying the
// dominator-tree direction (ie, dominator before the dominated).  This is,
// actually, easy to achieve: any pre-order CFG traversal algorithm will do it.
// Because such algorithms visit a block only after traversing some path to it
// from the root, they necessarily visit the block's idom first.
//
// But not every graph-traversal algorithm outputs blocks in an order that
// appears logical to human readers.  The problem is that unrelated branches may
// be interspersed with each other, and merge blocks may come before some of the
// branches being merged.
//
// A good, human-readable order of blocks may be achieved by performing
// depth-first search but delaying merge nodes until after all their branches
// have been visited.  This is implemented below by the inReadableOrder()
// function.

#include "spvIR.h"
#include "libretro/core_spv_walk.h"
#include <exception>

namespace {
struct WalkContext {
    const spv::Function* function;
    const std::function<void(spv::Block*, spv::ReachReason, spv::Block*)>* callback;
    std::exception_ptr exception;
};
static size_t nodeIndex(void* opaque, const void* node) {
    const auto& context = *static_cast<WalkContext*>(opaque);
    const auto* block = static_cast<const spv::Block*>(node);
    size_t index = block->getTraversalIndex();
    return &block->getParent() == context.function && index < context.function->getBlockCount()
        ? index : SIZE_MAX;
}
static size_t successorCount(const void* node) {
    return static_cast<const spv::Block*>(node)->getSuccessors().size();
}
static const void* successor(const void* node, size_t index) {
    return static_cast<const spv::Block*>(node)->getSuccessors()[index];
}
static int targets(const void* node, const void** merge, const void** continuation) {
    const auto* block = static_cast<const spv::Block*>(node);
    const auto* instruction = block->getMergeInstruction();
    if (!instruction) return 1;
    const auto& module = block->getParent().getParent();
    const auto* mergeInstruction = module.getInstruction(instruction->getIdOperand(0));
    if (!mergeInstruction || !mergeInstruction->getBlock()) return 0;
    *merge = mergeInstruction->getBlock();
    if (instruction->getOpCode() == spv::Op::OpLoopMerge) {
        const auto* continueInstruction = module.getInstruction(instruction->getIdOperand(1));
        if (!continueInstruction || !continueInstruction->getBlock()) return 0;
        *continuation = continueInstruction->getBlock();
    }
    return 1;
}
static int visit(void* opaque, const void* node, int reason, const void* header) noexcept {
    auto& context = *static_cast<WalkContext*>(opaque);
    try {
        (*context.callback)(const_cast<spv::Block*>(static_cast<const spv::Block*>(node)),
            static_cast<spv::ReachReason>(reason),
            const_cast<spv::Block*>(static_cast<const spv::Block*>(header)));
    } catch (...) {
        context.exception = std::current_exception();
        return 0;
    }
    return 1;
}
}

void spv::inReadableOrder(Block* root, std::function<void(Block*, ReachReason, Block*)> callback, xe_spv_plan* workspace)
{
    static_assert(int(ReachViaControlFlow) == XE_SPV_WALK_CONTROL &&
                  int(ReachDeadContinue) == XE_SPV_WALK_DEAD_CONTINUE &&
                  int(ReachDeadMerge) == XE_SPV_WALK_DEAD_MERGE);
    if (!root) throw std::bad_alloc();
    WalkContext context = {&root->getParent(), &callback, {}};
    const xe_spv_walk_ops operations = {nodeIndex, successorCount, successor, targets, visit};
    const size_t count = context.function->getBlockCount();
    int success;
    if (workspace && count > XE_SPV_WALK_INLINE) {
        success = xe_spv_walk_graph_states(&operations, &context, root, count,
            xe_spv_plan_states(workspace, count), workspace->capacity);
    } else {
        success = xe_spv_walk_graph(&operations, &context, root, count);
    }
    if (context.exception) std::rethrow_exception(context.exception);
    if (!success) throw std::bad_alloc();
}
