//
// Copyright (C) 2014 LunarG, Inc.
// Copyright (C) 2015-2018 Google, Inc.
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

// SPIRV-IR
//
// Simple in-memory representation (IR) of SPIRV.  Just for holding
// Each function's CFG of blocks.  Has this hierarchy:
//  - Module, which is a list of
//    - Function, which is a list of
//      - Block, which is a list of
//        - Instruction
//

#pragma once
#ifndef spvIR_H
#define spvIR_H

#include "spirv.hpp11"
#include "libretro/core_spv_operands.h"
#include "libretro/core_spv_binary.h"
#include "libretro/core_spv_size.h"
#include "libretro/core_spv_plan.h"
#include "libretro/core_spv_id_range.h"
#include "libretro/core_spv_block_chain.h"
#include "libretro/core_spv_instruction_chain.h"
#include "libretro/core_spv_block_state.h"
#include "libretro/core_spv_arena.h"
#include <cstddef>
#include <cstring>
#include <new>

#include <algorithm>
#include <cassert>
#include <functional>
#include <iostream>
#include <memory>
#include <vector>

namespace spv {

class Block;
class Function;
class Module;

const Id NoResult = 0;
const Id NoType = 0;

const Decoration NoPrecision = Decoration::Max;

#ifdef __GNUC__
#   define POTENTIALLY_UNUSED __attribute__((unused))
#else
#   define POTENTIALLY_UNUSED
#endif

POTENTIALLY_UNUSED
const MemorySemanticsMask MemorySemanticsAllMemory =
                (MemorySemanticsMask)(MemorySemanticsMask::UniformMemory |
                                      MemorySemanticsMask::WorkgroupMemory |
                                      MemorySemanticsMask::AtomicCounterMemory | 
                                      MemorySemanticsMask::ImageMemory);

struct IdImmediate {
    bool isId;      // true if word is an Id, false if word is an immediate
    unsigned word;
    IdImmediate(bool i, unsigned w) : isId(i), word(w) {}
    IdImmediate(bool i, spv::MemoryAccessMask w) : isId(i), word((unsigned)w) {}
    IdImmediate(bool i, spv::TensorAddressingOperandsMask w) : isId(i), word((unsigned)w) {}
    IdImmediate(bool i, spv::ImageOperandsMask w) : isId(i), word((unsigned)w) {}
    IdImmediate(bool i, spv::CooperativeMatrixOperandsMask w) : isId(i), word((unsigned)w) {}
};

//
// SPIR-V IR instruction.
//

class Instruction final {
public:
    Instruction(Id resultId, Id typeId, Op opCode) : resultId(resultId), typeId(typeId), opCode(opCode), arenaOffset(0), ownerOrNext(nullptr) { xe_spv_operands_init(&operands); }
    explicit Instruction(Op opCode) : resultId(NoResult), typeId(NoType), opCode(opCode), arenaOffset(0), ownerOrNext(nullptr) { xe_spv_operands_init(&operands); }
    ~Instruction() { if (operands.heap) xe_spv_operands_free(&operands); }
    // All heap records come from their module arena. Stack temporaries remain valid.
    static void* operator new(size_t) = delete;
    static void* operator new[](size_t) = delete;
    static void operator delete(void* record) noexcept { xe_spv_arena_retire(record); }
    void reserveOperands(size_t count) {
        if (count <= operands.capacity) return;
        if (!xe_spv_operands_reserve_with(&operands, count,
                arenaOffset ? xe_spv_arena_payload_realloc : nullptr,
                arenaOffset ? xe_spv_arena_record_owner(this, arenaOffset) : nullptr))
            throw std::bad_alloc();
    }
    void addIdOperand(Id id) {
        // ids can't be 0
        assert(id);
        if (operands.count == operands.capacity) growOperands();
        xe_spv_operands_append_reserved(&operands, id, 1);
    }
    // This method is potentially dangerous as it can break assumptions
    // about SSA and lack of forward references.
    void setIdOperand(unsigned idx, Id id) {
        assert(id);
        assert(xe_spv_operands_is_id(&operands, idx));
        xe_spv_operands_words(&operands)[idx] = id;
    }

    void addImmediateOperand(unsigned int immediate) {
        if (operands.count == operands.capacity) growOperands();
        xe_spv_operands_append_reserved(&operands, immediate, 0);
    }

    void addImmediateOperand(spv::StorageClass immediate) {
        addImmediateOperand((unsigned)immediate);
    }

    void addImmediateOperand(spv::ExecutionMode immediate) {
        addImmediateOperand((unsigned)immediate);
    }

    void addImmediateOperand(spv::ExecutionModel immediate) {
        addImmediateOperand((unsigned)immediate);
    }

    void addImmediateOperand(spv::Decoration immediate) {
        addImmediateOperand((unsigned)immediate);
    }

    void addImmediateOperand(spv::LinkageType immediate) {
        addImmediateOperand((unsigned)immediate);
    }

    void addImmediateOperand(spv::MemoryAccessMask immediate) {
        addImmediateOperand((unsigned)immediate);
    }

    void addImmediateOperand(spv::Capability immediate) {
        addImmediateOperand((unsigned)immediate);
    }

    void addImmediateOperand(spv::AddressingModel immediate) {
        addImmediateOperand((unsigned)immediate);
    }

    void addImmediateOperand(spv::MemoryModel immediate) {
        addImmediateOperand((unsigned)immediate);
    }

    void addImmediateOperand(spv::FPEncoding immediate) {
        addImmediateOperand((unsigned)immediate);
    }

    void addImmediateOperand(spv::SourceLanguage immediate) {
        addImmediateOperand((unsigned)immediate);
    }

    void addImmediateOperand(spv::Dim immediate) {
        addImmediateOperand((unsigned)immediate);
    }

    void addImmediateOperand(spv::FunctionControlMask immediate){
        addImmediateOperand((unsigned)immediate);
    }

    void addImmediateOperand(spv::SelectionControlMask immediate) {
        addImmediateOperand((unsigned)immediate);
    }

    void addImmediateOperand(spv::LoopControlMask immediate) {
        addImmediateOperand((unsigned)immediate);
    }

    void setImmediateOperand(unsigned idx, unsigned int immediate) {
        assert(!xe_spv_operands_is_id(&operands, idx));
        xe_spv_operands_words(&operands)[idx] = immediate;
    }

    void addStringOperand(const char* str)
    {
        if (!str) throw std::bad_alloc();
        addStringOperand(str, strlen(str));
    }
    void addStringOperand(const char* str, size_t bytes)
    {
        size_t capacity = xe_spv_operands_string_capacity(&operands, bytes);
        if (!capacity || (bytes && !str)) throw std::bad_alloc();
        reserveOperands(capacity);
        if (!xe_spv_operands_string(&operands, str, bytes)) throw std::bad_alloc();
    }
    bool isIdOperand(int op) const { return xe_spv_operands_is_id(&operands, op); }
    // Only labels carry block ownership. Other records use this slot as their
    // intrusive ownership link, preserving record size and arena tag offset.
    void setBlock(Block* b) { assert(opCode == Op::OpLabel); ownerOrNext = b; }
    Block* getBlock() const { return opCode == Op::OpLabel ? static_cast<Block*>(ownerOrNext) : nullptr; }
    Instruction* getNextInstruction() const { assert(opCode != Op::OpLabel && opCode != Op::OpFunction); return static_cast<Instruction*>(ownerOrNext); }
    static void** nextSlot(void* record) noexcept {
        auto* instruction = static_cast<Instruction*>(record);
        assert(instruction->opCode != Op::OpLabel && instruction->opCode != Op::OpFunction);
        return &instruction->ownerOrNext;
    }
    static void destroyLinked(void* record) noexcept { delete static_cast<Instruction*>(record); }
    Op getOpCode() const { return opCode; }
    int getNumOperands() const
    {
        return (int)operands.count;
    }
    Id getResultId() const { return resultId; }
    Id getTypeId() const { return typeId; }
    Id getIdOperand(int op) const {
        assert(xe_spv_operands_is_id(&operands, op));
        return xe_spv_operands_const_words(&operands)[op];
    }
    unsigned int getImmediateOperand(int op) const {
        assert(!xe_spv_operands_is_id(&operands, op));
        return xe_spv_operands_const_words(&operands)[op];
    }

    // Write out the binary form.
    void dump(std::vector<unsigned int>& out) const
    {
        // Compute the wordCount
        unsigned int wordCount = 1;
        if (typeId)
            ++wordCount;
        if (resultId)
            ++wordCount;
        wordCount += (unsigned int)operands.count;

        // Write out the beginning of the instruction
        out.push_back(((wordCount) << WordCountShift) | (unsigned)opCode);
        if (typeId)
            out.push_back(typeId);
        if (resultId)
            out.push_back(resultId);

        // Write out the operands
        for (int op = 0; op < (int)operands.count; ++op)
            out.push_back(xe_spv_operands_const_words(&operands)[op]);
    }

    void dump(xe_spv_binary& out) const
    {
        xe_spv_binary_instruction(&out, (uint32_t)opCode, typeId, resultId,
            xe_spv_operands_const_words(&operands), operands.count);
    }

void dump(xe_spv_count& out) const
    {
        xe_spv_count_instruction(&out, (uint32_t)opCode, typeId, resultId,
            operands.count);
    }

    const char *getNameString() const {
        if (opCode == Op::OpString) {
            return (const char *)&xe_spv_operands_const_words(&operands)[0];
        } else {
            assert(opCode == Op::OpName);
            return (const char *)&xe_spv_operands_const_words(&operands)[1];
        }
    }

protected:
    void growOperands() {
        size_t capacity = xe_spv_operands_growth_capacity(&operands);
        if (!capacity) throw std::bad_alloc();
        reserveOperands(capacity);
    }
    Instruction(const Instruction&) = delete;
    Instruction& operator=(const Instruction&) = delete;
    Id resultId;
    Id typeId;
    Op opCode;
    // Relative allocation-header offset occupies former alignment padding.
    uint32_t arenaOffset;
    friend class Module;
    friend class Function;
    xe_spv_operands operands;
    void* ownerOrNext;
};

//
// SPIR-V IR block.
//

class Block final : private xe_spv_block_link {
public:
    Instruction* createInstruction(Id result, Id type, Op code);
    Instruction* createInstruction(Op code);

    Block(Id id, Function& parent);
    static Block* create(Id id, Function& parent);
    static void* operator new(size_t) = delete;
    static void* operator new[](size_t) = delete;
    static void operator delete(void* record) noexcept { xe_spv_arena_object_retire(record); }
    ~Block() {
        xe_spv_instruction_destroy(localVariables.head, Instruction::nextSlot, Instruction::destroyLinked);
        label.reset();
        xe_spv_instruction_destroy(body.chain.head, Instruction::nextSlot, Instruction::destroyLinked);
    }

    Id getId() { return label->getResultId(); }

    Function& getParent() const { return parent; }
    // Returns true if the source location is actually updated.
    // Note we still need the builder to insert the line marker instruction. This is just a tracker.
    bool updateDebugSourceLocation(int line, int column, spv::Id fileId) {
        return xe_spv_block_source(&debugState, &stateFlags, line, column, fileId) != 0;
    }
    // Returns true when the debug scope changes.
    bool updateDebugScope(spv::Id scopeId) {
        assert(scopeId);
        return xe_spv_block_scope(&debugState, &stateFlags, scopeId) != 0;
    }
    void addInstruction(std::unique_ptr<Instruction> inst);
    void addPredecessor(Block* pred) { predecessors.push_back(pred); pred->successors.push_back(this);}
    void addLocalVariable(std::unique_ptr<Instruction> inst) {
        assert(inst && inst->getOpCode() != Op::OpLabel && !inst->getNextInstruction());
        xe_spv_instruction_append(&localVariables, inst.get(), Instruction::nextSlot);
        inst.release();
    }
    // previous/target are borrowed body nodes; the label is kept separately.
    void insertInstruction(Instruction* previous, Instruction* target, std::unique_ptr<Instruction> inst) {
        assert(inst && inst->getOpCode() != Op::OpLabel && !inst->getNextInstruction());
        if (!xe_spv_instruction_insert(&body, previous, target, inst.get(), Instruction::nextSlot))
            throw std::bad_alloc();
        inst.release();
    }
    const std::vector<Block*>& getPredecessors() const { return predecessors; }
    const std::vector<Block*>& getSuccessors() const { return successors; }
    Instruction* getFirstInstruction() const { return label.get(); }
    Instruction* getFirstBodyInstruction() const { return static_cast<Instruction*>(body.chain.head); }
    Instruction* getNextInstruction(const Instruction* instruction) const {
        return instruction == label.get() ? getFirstBodyInstruction() : instruction->getNextInstruction();
    }
    Instruction* getFirstLocalVariable() const { return static_cast<Instruction*>(localVariables.head); }
    void setUnreachable() { stateFlags = (uint8_t)(stateFlags | XE_SPV_BLOCK_UNREACHABLE); }
    bool isUnreachable() const { return (stateFlags & XE_SPV_BLOCK_UNREACHABLE) != 0; }
    uint32_t getTraversalIndex() const { return traversalIndex; }
    Block* getNextBlock() const { return static_cast<Block*>(next); }
    // Returns the block's merge instruction, if one exists (otherwise null).
    const Instruction* getMergeInstruction() const {
        const Instruction* nextToLast = static_cast<Instruction*>(body.penultimate);
        if (!nextToLast) return nullptr;
        switch (nextToLast->getOpCode()) {
            case Op::OpSelectionMerge:
            case Op::OpLoopMerge:
                return nextToLast;
            default:
                return nullptr;
        }
        return nullptr;
    }

    // Change this block into a canonical dead merge block.  Delete instructions
    // as necessary.  A canonical dead merge block has only an OpLabel and an
    // OpUnreachable.
    void rewriteAsCanonicalUnreachableMerge() {
        assert(!localVariables.head);
        // Delete all instructions except for the label.
        clearBody();
        successors.clear();
        addInstruction(std::unique_ptr<Instruction>(createInstruction(Op::OpUnreachable)));
    }
    // Change this block into a canonical dead continue target branching to the
    // given header ID.  Delete instructions as necessary.  A canonical dead continue
    // target has only an OpLabel and an unconditional branch back to the corresponding
    // header.
    void rewriteAsCanonicalUnreachableContinue(Block* header) {
        assert(!localVariables.head);
        // Delete all instructions except for the label.
        clearBody();
        successors.clear();
        // Add OpBranch back to the header.
        assert(header != nullptr);
        Instruction* branch = createInstruction(Op::OpBranch);
        branch->addIdOperand(header->getId());
        addInstruction(std::unique_ptr<Instruction>(branch));
        successors.push_back(header);
    }

    bool isTerminated() const
    {
        switch ((body.chain.tail ? static_cast<Instruction*>(body.chain.tail) : label.get())->getOpCode()) {
        case Op::OpBranch:
        case Op::OpBranchConditional:
        case Op::OpSwitch:
        case Op::OpKill:
        case Op::OpTerminateInvocation:
        case Op::OpReturn:
        case Op::OpReturnValue:
        case Op::OpUnreachable:
            return true;
        default:
            return false;
        }
    }

    template <class Output> void dump(Output& out) const
    {
        label->dump(out);
        for (Instruction* i = getFirstLocalVariable(); i; i = i->getNextInstruction())
            i->dump(out);
        for (Instruction* i = getFirstBodyInstruction(); i; i = i->getNextInstruction())
            i->dump(out);
    }

protected:
    Block(const Block&) = delete;
    Block& operator=(Block&) = delete;

    // To enforce keeping parent and ownership in sync:
    friend Function;

    // Keep the traversal inputs together; predecessor/debug data is colder.
    xe_spv_instruction_body body = {};
    std::unique_ptr<Instruction> label;
    std::vector<Block*> successors;
    // Dense creation index and owner are read on every traversal visit.
    uint32_t traversalIndex = UINT32_MAX;
    uint8_t stateFlags;
    Function& parent;
    std::vector<Block*> predecessors;
    xe_spv_instruction_chain localVariables = {};

    void clearBody() noexcept {
        xe_spv_instruction_destroy(body.chain.head, Instruction::nextSlot, Instruction::destroyLinked);
        body = {};
    }

    // Read only when the corresponding valid bit is present in stateFlags.
    xe_spv_block_debug debugState;

    friend class Function;
};

// The different reasons for reaching a block in the inReadableOrder traversal.
enum ReachReason {
    // Reachable from the entry block via transfers of control, i.e. branches.
    ReachViaControlFlow = 0,
    // A continue target that is not reachable via control flow.
    ReachDeadContinue,
    // A merge block that is not reachable via control flow.
    ReachDeadMerge
};

// Traverses the control-flow graph rooted at root in an order suited for
// readable code generation.  Invokes callback at every node in the traversal
// order.  The callback arguments are:
// - the block,
// - the reason we reached the block,
// - if the reason was that block is an unreachable continue or unreachable merge block
//   then the last parameter is the corresponding header block.
void inReadableOrder(Block* root, std::function<void(Block*, ReachReason, Block* header)> callback, xe_spv_plan* workspace = nullptr);

//
// SPIR-V IR Function.
//

class Function final {
public:
    Instruction* createInstruction(Id result, Id type, Op code);
    Instruction* createInstruction(Op code);

    Function(Id id, Id resultType, Id functionType, Id firstParam, LinkageType linkage, const char* name, Module& parent);
    ~Function();
    static Function* create(Id id, Id resultType, Id functionType, Id firstParam, LinkageType linkage, const char* name, Module& parent);
    static void* operator new(size_t) = delete;
    static void* operator new[](size_t) = delete;
    static void operator delete(void* record) noexcept { xe_spv_arena_object_retire(record); }
    Id getId() const { return functionInstruction.getResultId(); }
    // OpFunction's inline header never belongs to an instruction body/list.
    // Reuse its link slot with the actual Function type, without a new field.
    Function* getNextFunction() const {
        return static_cast<Function*>(functionInstruction.ownerOrNext);
    }
    static void** nextSlot(void* record) noexcept {
        return &static_cast<Function*>(record)->functionInstruction.ownerOrNext;
    }
    static void destroyLinked(void* record) noexcept { delete static_cast<Function*>(record); }
    Id getParamId(int p) const { return xe_spv_id_range_get(&parameters, (size_t)p); }
    Id getParamType(int p) const;

    void addBlock(Block* block) {
        if (!block || &block->getParent() != this || block->traversalIndex != UINT32_MAX ||
            blocks.count >= UINT32_MAX || !xe_spv_block_chain_append(&blocks, block))
            throw std::bad_alloc();
        block->traversalIndex = (uint32_t)(blocks.count - 1);
    }

    Module& getParent() const { return parent; }
    Block* getEntryBlock() const { return static_cast<Block*>(blocks.head); }
    Block* getLastBlock() const { return static_cast<Block*>(blocks.tail); }
    size_t getBlockCount() const { return blocks.count; }
    void addLocalVariable(std::unique_ptr<Instruction> inst);
    Id getReturnType() const { return functionInstruction.getTypeId(); }
    Id getFuncId() const { return functionInstruction.getResultId(); }
    Id getFuncTypeId() const { return functionInstruction.getIdOperand(1); }
    void setReturnPrecision(Decoration precision)
    {
        if (precision == Decoration::RelaxedPrecision)
            reducedPrecisionReturn = true;
    }
    Decoration getReturnPrecision() const
        { return reducedPrecisionReturn ? Decoration::RelaxedPrecision : NoPrecision; }

    void setDebugLineInfo(Id fileName, int line, int column) {
        lineInstruction = std::unique_ptr<Instruction>{createInstruction(Op::OpLine)};
        lineInstruction->reserveOperands(3);
        lineInstruction->addIdOperand(fileName);
        lineInstruction->addImmediateOperand(line);
        lineInstruction->addImmediateOperand(column);
    }
    bool hasDebugLineInfo() const { return lineInstruction != nullptr; }

    void setImplicitThis() { implicitThis = true; }
    bool hasImplicitThis() const { return implicitThis; }

    template <class Output> void dump(Output& out, xe_spv_plan* plan = nullptr, bool record = false) const
    {
        // OpLine
        if (lineInstruction != nullptr) {
            lineInstruction->dump(out);
        }

        // OpFunction
        functionInstruction.dump(out);

        // OpFunctionParameter
        if (parameters.count) {
            void* const* parameterRecords = getParamInstructions();
            for (uint32_t p = 0; p < parameters.count; ++p)
                static_cast<Instruction*>(parameterRecords[p])->dump(out);
        }

        // Blocks
        if (plan && !record) {
            const void* node;
            for (;;) {
                if (!xe_spv_plan_next(plan, &node)) throw std::bad_alloc();
                if (!node) break;
                static_cast<const Block*>(node)->dump(out);
            }
        } else {
            if (plan && !xe_spv_plan_function(plan, blocks.count)) throw std::bad_alloc();
            inReadableOrder(getEntryBlock(), [&](const Block* b, ReachReason, Block*) {
                if (plan && !xe_spv_plan_push(plan, b)) throw std::bad_alloc();
                b->dump(out);
            }, plan);
            if (plan && !xe_spv_plan_push(plan, nullptr)) throw std::bad_alloc();
        }
        Instruction end(0, 0, Op::OpFunctionEnd);
        end.dump(out);
    }

    LinkageType getLinkType() const { return linkType; }
    const char* getExportName() const { return exportName.c_str(); }

protected:
    Function(const Function&) = delete;
    Function& operator=(const Function&) = delete;
    Function& operator=(Function&);

    Module& parent;
    std::unique_ptr<Instruction> lineInstruction;
    Instruction functionInstruction;
    Instruction* getParamInstruction(size_t index) const;
    void* const* getParamInstructions() const;
    xe_spv_id_range parameters = {};
    static void destroyBlock(xe_spv_block_link* link) noexcept { delete static_cast<Block*>(link); }
    xe_spv_block_chain blocks = {};
    bool implicitThis;  // true if this is a member function expecting to be passed a 'this' as the first argument
    bool reducedPrecisionReturn;
    LinkageType linkType;
    std::string exportName;
};

//
// SPIR-V IR Module.
//

class Module final {
public:
    explicit Module(xe_spv_arena* externalArena = nullptr)
        : arena(externalArena ? externalArena : &localArena), borrowed(externalArena != nullptr)
    {
        static_assert(sizeof(Instruction) <= XE_SPV_RECORD_BYTES);
        static_assert(offsetof(Instruction, arenaOffset) == XE_SPV_RECORD_ARENA_OFFSET);
        if (!xe_spv_arena_begin(arena)) throw std::bad_alloc();
    }
    // Builder fields are destroyed first. Global instruction lists only borrow
    // arena records; sweep their payloads and exceptional orphans last. Their
    // destructor only frees operand storage, so no cross-record order is needed.
    ~Module()
    {
        // Detached blocks only release their own payloads; their parent function
        // may already be gone. Complete typed cleanup before sweeping instructions.
        const bool objectsEnded = xe_spv_arena_objects_end(arena, destroyFunction, destroyBlock) != 0;
        assert(objectsEnded);
        const bool ended = objectsEnded && xe_spv_arena_finish(arena, destroyRecord) != 0;
        assert(ended);
        if (!borrowed && ended) {
            const bool closed = xe_spv_arena_close(arena) != 0;
            assert(closed);
            (void)closed;
        }
    }
    void* allocateObject(unsigned kind, size_t bytes) {
        void* record = xe_spv_arena_object_alloc(arena, kind, bytes);
        if (!record) throw std::bad_alloc();
        return record;
    }
    Instruction* createInstruction(Id result, Id type, Op code)
    {
        uint32_t offset;
        void* memory = xe_spv_arena_alloc(arena, &offset);
        if (!memory) throw std::bad_alloc();
        Instruction* instruction = ::new (memory) Instruction(result, type, code);
        instruction->arenaOffset = offset;
        return instruction;
    }
    Instruction* createInstruction(Op code) { return createInstruction(NoResult, NoType, code); }


    bool ownsInstruction(const Instruction* instruction) const {
        return xe_spv_arena_owns_record(arena, instruction) != 0;
    }
    void forgetInstruction(Id id) { xe_spv_index_unmap(&arena->index, id); }
    void mapInstruction(Instruction *instruction)
    {
        if (!xe_spv_index_set(&arena->index, instruction->getResultId(), instruction))
            throw std::bad_alloc();
    }

    void* const* borrowInstructions(Id first, size_t count) const {
        return xe_spv_index_borrow(&arena->index, first, count);
    }
    Instruction* getInstruction(Id id) const {
        return static_cast<Instruction*>(xe_spv_index_get(&arena->index, id));
    }
    spv::Id getTypeId(Id resultId) const {
        const Instruction* instruction = getInstruction(resultId);
        return instruction ? instruction->getTypeId() : NoType;
    }
    StorageClass getStorageClass(Id typeId) const
    {
        const Instruction* instruction = getInstruction(typeId);
        assert(instruction && instruction->getOpCode() == spv::Op::OpTypePointer);
        return (StorageClass)instruction->getImmediateOperand(0);
    }

protected:
    static void destroyFunction(void* record) { delete static_cast<Function*>(record); }
    static void destroyBlock(void* record) { delete static_cast<Block*>(record); }
    static void destroyRecord(void* record) { static_cast<Instruction*>(record)->~Instruction(); }
    xe_spv_arena localArena = {};
    xe_spv_arena* arena;
    bool borrowed;
    Module(const Module&) = delete;
    Module& operator=(const Module&) = delete;
    // Instruction lookup storage belongs to the exclusive arena workspace.
};

//
// Implementation (it's here due to circular type definitions).
//

__inline Instruction* Block::createInstruction(Id result, Id type, Op code)
{ return parent.getParent().createInstruction(result, type, code); }
__inline Instruction* Block::createInstruction(Op code)
{ return parent.getParent().createInstruction(code); }
__inline Instruction* Function::createInstruction(Id result, Id type, Op code)
{ return parent.createInstruction(result, type, code); }
__inline Instruction* Function::createInstruction(Op code)
{ return parent.createInstruction(code); }

__inline Instruction* Function::getParamInstruction(size_t index) const
{
    Id id = xe_spv_id_range_get(&parameters, index);
    return id ? parent.getInstruction(id) : nullptr;
}
__inline Id Function::getParamType(int index) const
{
    const Instruction* instruction = getParamInstruction((size_t)index);
    return instruction ? instruction->getTypeId() : NoType;
}
__inline void* const* Function::getParamInstructions() const
{ return parent.borrowInstructions(parameters.first, parameters.count); }
__inline Block* Block::create(Id id, Function& parent)
{
    static_assert(alignof(Block) <= 16);
    void* record = parent.getParent().allocateObject(XE_SPV_OBJECT_BLOCK, sizeof(Block));
    try { return ::new (record) Block(id, parent); }
    catch (...) { xe_spv_arena_object_retire(record); throw; }
}
__inline Function* Function::create(Id id, Id resultType, Id functionType, Id firstParam,
    LinkageType linkage, const char* name, Module& parent)
{
    static_assert(alignof(Function) <= 16);
    void* record = parent.allocateObject(XE_SPV_OBJECT_FUNCTION, sizeof(Function));
    try { return ::new (record) Function(id, resultType, functionType, firstParam, linkage, name, parent); }
    catch (...) { parent.forgetInstruction(id); xe_spv_arena_object_retire(record); throw; }
}
__inline Function::~Function()
{
    parent.forgetInstruction(getId());
    uint32_t p;
    if (parameters.count) {
        void* const* parameterRecords = getParamInstructions();
        for (p = 0; p < parameters.count; ++p)
            delete static_cast<Instruction*>(parameterRecords[p]);
    }
    xe_spv_block_chain_destroy(&blocks, destroyBlock);
}

// Add both
// - the OpFunction instruction
// - all the OpFunctionParameter instructions
__inline Function::Function(Id id, Id resultType, Id functionType, Id firstParamId, LinkageType linkage, const char* name, Module& parent)
    : parent(parent), lineInstruction(nullptr),
      functionInstruction(id, resultType, Op::OpFunction), implicitThis(false),
      reducedPrecisionReturn(false),
      linkType(linkage)
{
    // OpFunction
    functionInstruction.reserveOperands(2);
    functionInstruction.addImmediateOperand(FunctionControlMask::MaskNone);
    functionInstruction.addIdOperand(functionType);
    parent.mapInstruction(&functionInstruction);

    // OpFunctionParameter
    Instruction* typeInst = parent.getInstruction(functionType);
    int numParams = typeInst->getNumOperands() - 1;
    if (numParams && !xe_spv_id_range_set(&parameters, firstParamId, (size_t)numParams))
        throw std::bad_alloc();
    for (int p = 0; p < numParams; ++p) {
        Instruction* param = parent.createInstruction(firstParamId + p, typeInst->getIdOperand(p + 1), Op::OpFunctionParameter);
        parent.mapInstruction(param);
    }

    // If importing/exporting, save the function name (without the mangled parameters) for the linkage decoration
    if (linkType != LinkageType::Max) {
        const char* text = name ? name : "";
        exportName.assign(text, std::strcspn(text, "("));
    }
}

__inline void Function::addLocalVariable(std::unique_ptr<Instruction> inst)
{
    Instruction* raw_instruction = inst.get();
    getEntryBlock()->addLocalVariable(std::move(inst));
    parent.mapInstruction(raw_instruction);
}

__inline Block::Block(Id id, Function& parent) : xe_spv_block_link{}, stateFlags(0), parent(parent)
{
    label.reset(createInstruction(id, NoType, Op::OpLabel));
    label->setBlock(this);
    parent.getParent().mapInstruction(label.get());
}

__inline void Block::addInstruction(std::unique_ptr<Instruction> inst)
{
    Instruction* raw_instruction = inst.get();
    assert(inst && inst->getOpCode() != Op::OpLabel && !inst->getNextInstruction());
    xe_spv_instruction_body_append(&body, raw_instruction, Instruction::nextSlot);
    inst.release();
    if (raw_instruction->getResultId())
        parent.getParent().mapInstruction(raw_instruction);
}

}  // end spv namespace

#endif // spvIR_H
