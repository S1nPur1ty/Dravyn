// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstdlib>
#include <fstream>
#include <catch2/catch_test_macros.hpp>
#include "shader_recompiler/backend/spirv/emit_spirv.h"
#include "shader_recompiler/frontend/ir/ir_emitter.h"
#include "shader_recompiler/profile.h"

TEST_CASE("Storage-backed shared memory preserves addressing and barriers", "[shared-memory]") {
    namespace IR = Shader::IR;
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    IR::IREmitter ir{block};
    Shader::IR::Program program;
    program.stage = Shader::Stage::Compute;
    program.workgroup_size = {32, 1, 1};
    program.shared_memory_size = 49152;
    program.info.shared_memory_storage_size = 49152;
    program.info.uses_workgroup_id = true;
    program.info.uses_local_invocation_id = true;
    program.info.uses_int8 = true;
    program.info.uses_int16 = true;
    program.info.uses_shared_increment = true;
    program.info.used_storage_buffer_types = IR::Type::U32;
    program.info.storage_buffers_descriptors.push_back({0, 0, 1, true});
    const auto add = [&](IR::U32 a, IR::U32 b) { return IR::U32{ir.IAdd(a, b)}; };
    const auto imm = [&](u32 x) { return ir.Imm32(x); };
    const auto lane = ir.LocalInvocationIdX();
    const auto group = add(add(ir.WorkgroupIdX(), ir.IMul(ir.WorkgroupIdY(), imm(2))),
                           ir.IMul(ir.WorkgroupIdZ(), imm(4)));
    const auto value = add(add(ir.IMul(group, imm(100)), lane), imm(1));
    const auto offset = add(imm(32768), ir.IMul(lane, imm(16)));
    ir.WriteShared(128, offset, ir.CompositeConstruct(value, value, value, value));
    const auto subword = add(imm(40000), ir.IMul(lane, imm(4)));
    ir.WriteShared(8, subword, imm(0x5a));
    ir.WriteShared(16, add(subword, imm(2)), imm(0x1234));
    const auto atomic = add(imm(45000), ir.IMul(lane, imm(4)));
    ir.WriteShared(32, atomic, imm(0));
    ir.Barrier();
    const auto ignored = ir.SharedAtomicInc(atomic, imm(500));
    ir.Reference(ignored);
    ir.Barrier();
    const auto neighbour = ir.BitwiseAnd(add(lane, imm(1)), imm(31));
    const auto loaded = ir.LoadShared(128, false, add(imm(32768), ir.IMul(neighbour, imm(16))));
    const auto result = ir.IMul(add(ir.IMul(group, imm(32)), lane), imm(20));
    const auto write = [&](u32 word, IR::Value v) {
        block.AppendNewInst(IR::Opcode::WriteStorage32, {imm(0), add(result, imm(word * 4)), v});
    };
    write(0, ir.CompositeExtract(loaded, 0));
    write(1, ir.CompositeExtract(loaded, 3));
    write(2, ir.LoadShared(8, false, subword));
    write(3, ir.LoadShared(16, false, add(subword, imm(2))));
    write(4, ir.LoadShared(32, false, atomic));
    program.blocks = {&block};
    program.post_order_blocks = {&block};
    program.syntax_list.push_back(
        {.data = {.block = &block}, .type = IR::AbstractSyntaxNode::Type::Block});
    program.syntax_list.push_back({.type = IR::AbstractSyntaxNode::Type::Return});
    Shader::Profile profile{};
    profile.supported_spirv = 0x00010300;
    profile.unified_descriptor_binding = true;
    const auto spirv = Shader::Backend::SPIRV::EmitSPIRV(profile, program);
    REQUIRE(spirv.size() > 5);
    if (const char* path = std::getenv("EDEN_TEST_SHARED_SPIRV")) {
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(spirv.data()), spirv.size() * sizeof(u32));
        REQUIRE(file.good());
    }
}
