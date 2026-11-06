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
#include "libretro/core_spv_packet.h"
#include "libretro/core_spv_module.h"
#include <type_traits>
#include "libretro/core_spv_words.h"
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
    Instruction(Id resultId, Id typeId, Op opCode) : ownerOrNext(nullptr) { xe_spv_packet_init(&packet, (uint32_t)opCode, typeId, resultId); }
    explicit Instruction(Op opCode) : Instruction(NoResult, NoType, opCode) {}
    ~Instruction() { if (packet.heap) xe_spv_packet_free(&packet); }
    // All heap records come from their module arena. Stack temporaries remain valid.
    static void* operator new(size_t) = delete;
    static void* operator new[](size_t) = delete;
    static void operator delete(void* record) noexcept { xe_spv_arena_retire(record); }
    void reserveOperands(size_t count) {
        if (count <= packet.capacity) return;
        if (!xe_spv_packet_reserve_with(&packet, count,
                packet.arena_offset ? xe_spv_arena_payload_realloc : nullptr,
                packet.arena_offset ? xe_spv_arena_record_owner(this, packet.arena_offset) : nullptr))
            throw std::bad_alloc();
    }
    void addIdOperand(Id id) {
        // ids can't be 0
        assert(id);
        if (packet.count == packet.capacity) growOperands();
        xe_spv_packet_append_reserved(&packet, id, 1);
    }
    // This method is potentially dangerous as it can break assumptions
    // about SSA and lack of forward references.
    void setIdOperand(unsigned idx, Id id) {
        assert(id);
        assert(xe_spv_packet_is_id(&packet, idx));
        xe_spv_packet_operands(&packet)[idx] = id;
    }

    void addImmediateOperand(unsigned int immediate) {
        if (packet.count == packet.capacity) growOperands();
        xe_spv_packet_append_reserved(&packet, immediate, 0);
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
        assert(!xe_spv_packet_is_id(&packet, idx));
        xe_spv_packet_operands(&packet)[idx] = immediate;
    }

    void addStringOperand(const char* str)
    {
        if (!str) throw std::bad_alloc();
        addStringOperand(str, strlen(str));
    }
    void addStringOperand(const char* str, size_t bytes)
    {
        size_t capacity = xe_spv_packet_string_capacity(&packet, bytes);
        if (!capacity || (bytes && !str)) throw std::bad_alloc();
        reserveOperands(capacity);
        if (!xe_spv_packet_string_reserved(&packet, str, bytes)) throw std::bad_alloc();
    }
    bool isIdOperand(int op) const { return xe_spv_packet_is_id(&packet, op); }
    // Only labels carry block ownership. Other records use this slot as their
    // intrusive ownership link, preserving record size and arena tag offset.
    void setBlock(Block* b) { assert(getOpCode() == Op::OpLabel); ownerOrNext = b; }
    Block* getBlock() const { return getOpCode() == Op::OpLabel ? static_cast<Block*>(ownerOrNext) : nullptr; }
    Instruction* getNextInstruction() const { assert(getOpCode() != Op::OpLabel && getOpCode() != Op::OpFunction); return static_cast<Instruction*>(ownerOrNext); }
    static void** nextSlot(void* record) noexcept {
        auto* instruction = static_cast<Instruction*>(record);
        assert(instruction->getOpCode() != Op::OpLabel && instruction->getOpCode() != Op::OpFunction);
        return &instruction->ownerOrNext;
    }
    static void destroyLinked(void* record) noexcept { delete static_cast<Instruction*>(record); }
    Op getOpCode() const { return static_cast<Op>(xe_spv_packet_opcode(&packet)); }
    int getNumOperands() const
    {
        return (int)packet.count;
    }
    Id getResultId() const { return xe_spv_packet_result(&packet); }
    Id getTypedResultId() const { return xe_spv_packet_typed_result(&packet); }
    bool matchesConstant(uint32_t opcode, Id type, xe_spv_word_view words) const {
        return xe_spv_packet_constant_equal(&packet, opcode, type, words.words, words.count) != 0;
    }
    bool matchesRepeatedConstant(uint32_t opcode, Id type, Id word, size_t count) const {
        return xe_spv_packet_constant_repeat_equal(&packet, opcode, type, word, count) != 0;
    }
    void addSourceIdOperands(xe_spv_word_source source, size_t count) {
        size_t capacity = xe_spv_packet_source_capacity(&packet, count);
        if ((!capacity && count) || count > source.count) throw std::bad_alloc();
        reserveOperands(capacity);
        xe_spv_packet_source_ids_reserved(&packet, source, count);
    }
    xe_spv_word_view getOperandsView() const {
        return xe_spv_words_view(xe_spv_packet_const_operands(&packet), packet.count);
    }
    Id getTypeId() const { return xe_spv_packet_type(&packet); }
    Id getIdOperand(int op) const {
        assert(xe_spv_packet_is_id(&packet, op));
        return xe_spv_packet_const_operands(&packet)[op];
    }
    unsigned int getImmediateOperand(int op) const {
        assert(!xe_spv_packet_is_id(&packet, op));
        return xe_spv_packet_const_operands(&packet)[op];
    }

    // Write out the binary form.
    void dump(std::vector<unsigned int>& out) const
    {
        if (!xe_spv_packet_valid(&packet)) throw std::bad_alloc();
        const xe_spv_word_view words = xe_spv_packet_view(&packet);
        out.insert(out.end(), words.words, words.words + words.count);
    }

    void dump(xe_spv_binary& out) const
    {
        if (!xe_spv_packet_valid(&packet)) { out.failed = 1; return; }
        const xe_spv_word_view words = xe_spv_packet_view(&packet);
        xe_spv_binary_append(&out, words.words, words.count);
    }

    void dump(xe_spv_count& out) const
    {
        if (!xe_spv_packet_valid(&packet)) { out.failed = 1; return; }
        xe_spv_count_words(&out, xe_spv_packet_prefix(&packet) + packet.count);
    }

    const char *getNameString() const {
        if (getOpCode() == Op::OpString) {
            return (const char *)&xe_spv_packet_const_operands(&packet)[0];
        } else {
            assert(getOpCode() == Op::OpName);
            return (const char *)&xe_spv_packet_const_operands(&packet)[1];
        }
    }

protected:
    void growOperands() {
        size_t capacity = xe_spv_packet_growth(&packet);
        if (!capacity) throw std::bad_alloc();
        reserveOperands(capacity);
    }
    Instruction(const Instruction&) = delete;
    Instruction& operator=(const Instruction&) = delete;
    xe_spv_packet packet;
    friend class Module;
    friend class Function;
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

    Function(Id id, Id resultType, Id functionType, Id firstParam, LinkageType linkage, Module& parent);
    ~Function();
    static Function* create(Id id, Id resultType, Id functionType, Id firstParam, LinkageType linkage, Module& parent);
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
    // Borrow immutable OpName storage. Module instruction payloads outlive us;
    // the counted prefix excludes mangled parameters without a second copy.
    void setLinkageName(const Instruction* name) {
        linkageName = name ? name->getNameString() : nullptr;
        linkageNameBytes = linkageName ? std::strcspn(linkageName, "(") : 0;
    }
    const char* getLinkageNameData() const { return linkageName; }
    size_t getLinkageNameBytes() const { return linkageNameBytes; }

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
    const char* linkageName = nullptr;
    size_t linkageNameBytes = 0;
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
        static_assert(std::is_standard_layout_v<Instruction> && offsetof(Instruction, packet) == 0);
        static_assert(offsetof(Instruction, packet) + offsetof(xe_spv_packet, arena_offset) == XE_SPV_RECORD_ARENA_OFFSET);
        if (!xe_spv_arena_begin(arena)) throw std::bad_alloc();
    }
    // Builder fields are destroyed first. Global instruction lists only borrow
    // arena records; sweep their payloads and exceptional orphans last. Their
    // destructor only frees operand storage, so no cross-record order is needed.
    ~Module()
    {
        xe_spv_arena_clear_stacks(arena);
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
        instruction->packet.arena_offset = offset;
        return instruction;
    }
    Instruction* createInstruction(Op code) { return createInstruction(NoResult, NoType, code); }


    bool ownsInstruction(const Instruction* instruction) const {
        return xe_spv_arena_owns_record(arena, instruction) != 0;
    }
    void forgetInstruction(Id id) { xe_spv_index_unmap(&arena->index, id); }
    void* stackPush(unsigned kind, size_t bytes) {
        void* frame = xe_spv_arena_stack_push(arena, kind, bytes);
        if (!frame) throw std::bad_alloc();
        return frame;
    }
    void* stackTop(unsigned kind) const { return xe_spv_arena_stack_top(arena, kind); }
    bool stackPop(unsigned kind) { return xe_spv_arena_stack_pop(arena, kind) != 0; }
    void stackClear(unsigned kind) noexcept { xe_spv_arena_stack_clear(arena, kind); }
    void clearStacks() noexcept { xe_spv_arena_clear_stacks(arena); }
    void addExtension(const char* name) {
        if (!xe_spv_arena_add_extension(arena, name)) throw std::bad_alloc();
    }
    void removeExtension(const char* name) { xe_spv_arena_remove_extension(arena, name); }
    xe_spv_record_view extensionRecords() const { return xe_spv_arena_extensions(arena); }
    void addDecoration(const xe_spv_decoration_key& key) {
        if (!xe_spv_arena_add_decoration(arena, &key)) throw std::bad_alloc();
    }
    xe_spv_record_view decorationRecords() const { return xe_spv_arena_decorations(arena); }
    void addCapability(uint32_t value) {
        if (!xe_spv_arena_add_capability(arena, value)) throw std::bad_alloc();
    }
    bool hasCapability(uint32_t value) const { return xe_spv_arena_has_capability(arena, value) != 0; }
    xe_spv_word_view capabilityWords() const { return xe_spv_arena_capabilities(arena); }
    xe_spv_record_view typeRecords(uint32_t opcode, bool debug = false) const {
        return xe_spv_arena_group_view(arena, xe_spv_group_key(opcode, debug));
    }
    void appendTypeRecord(uint32_t opcode, Instruction* record, bool debug = false) {
        if (!xe_spv_arena_group_append(arena, xe_spv_group_key(opcode, debug), record))
            throw std::bad_alloc();
    }
    xe_spv_constant_cache* constantCache(size_t extra = 0) {
        xe_spv_constant_cache* cache = xe_spv_arena_constants(arena, extra);
        if (!cache) throw std::bad_alloc();
        return cache;
    }
    void mapInstruction(Instruction *instruction)
    {
        if (!xe_spv_module_map(&arena->index, &instruction->packet))
            throw std::bad_alloc();
    }

    void* const* borrowInstructions(Id first, size_t count) const {
        return xe_spv_index_borrow(&arena->index, first, count);
    }
    Instruction* getInstruction(Id id) const {
        return reinterpret_cast<Instruction*>(xe_spv_module_get(&arena->index, id));
    }
    spv::Id getTypeId(Id resultId) const {
        return xe_spv_module_type(&arena->index, resultId);
    }
    StorageClass getStorageClass(Id typeId) const
    {
        static_assert(static_cast<uint32_t>(spv::Op::OpTypePointer) == XE_SPV_MODULE_TYPE_POINTER);
        uint32_t storage = static_cast<uint32_t>(StorageClass::Max);
        const bool valid = xe_spv_module_storage_class(&arena->index, typeId, &storage) != 0;
        assert(valid);
        (void)valid;
        return static_cast<StorageClass>(storage);
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
    LinkageType linkage, Module& parent)
{
    static_assert(alignof(Function) <= 16);
    void* record = parent.allocateObject(XE_SPV_OBJECT_FUNCTION, sizeof(Function));
    try { return ::new (record) Function(id, resultType, functionType, firstParam, linkage, parent); }
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
__inline Function::Function(Id id, Id resultType, Id functionType, Id firstParamId, LinkageType linkage, Module& parent)
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
