// SPDX-License-Identifier: GPL-3.0-or-later

#include <atomic>
#include <barrier>
#include <cstring>
#include <memory>
#include <thread>

#include <catch2/catch_test_macros.hpp>
#include <oaknut/oaknut.hpp>

#include "dynarmic/interface/exclusive_monitor.h"
#include "dynarmic/tests/A64/testenv.h"

#ifdef ARCHITECTURE_arm64
namespace {
using namespace Dynarmic;
using namespace oaknut::util;

class ExclusiveEnv : public A64TestEnv {
public:
    alignas(4096) std::array<u8, 8192> storage{};
    u8* memory = storage.data();
    std::array<uintptr_t, 32> table{};
    u32 reads{};
    u32 writes{};
    size_t host_offset{};

    u8* Pointer(u64 address) {
        return memory + host_offset + (address & 0xfff);
    }

    u64 MemoryRead(u64 address, size_t size) override {
        ++reads;
        u64 result{};
        std::memcpy(&result, Pointer(address), size);
        return result;
    }

    Vector MemoryRead128(u64 address) override {
        ++reads;
        Vector result;
        std::memcpy(result.data(), Pointer(address), sizeof(result));
        return result;
    }

    template<typename T>
    bool Store(u64 address, T value, T expected) {
        ++writes;
        auto* pointer = Pointer(address);
        if (reinterpret_cast<uintptr_t>(pointer) % alignof(T) == 0) {
            return std::atomic_ref<T>(*reinterpret_cast<T*>(pointer)).compare_exchange_strong(expected, value);
        }
        if (std::memcmp(pointer, &expected, sizeof(T)) != 0) {
            return false;
        }
        std::memcpy(pointer, &value, sizeof(T));
        return true;
    }

    bool MemoryWriteExclusive8(u64 a, u8 v, u8 e) override { return Store(a, v, e); }
    bool MemoryWriteExclusive16(u64 a, u16 v, u16 e) override { return Store(a, v, e); }
    bool MemoryWriteExclusive32(u64 a, u32 v, u32 e) override { return Store(a, v, e); }
    bool MemoryWriteExclusive64(u64 a, u64 v, u64 e) override { return Store(a, v, e); }
    bool MemoryWriteExclusive128(u64 address, Vector value, Vector expected) override {
        ++writes;
        if (std::memcmp(Pointer(address), expected.data(), sizeof(expected)) != 0) {
            return false;
        }
        std::memcpy(Pointer(address), value.data(), sizeof(value));
        return true;
    }

    A64::UserConfig Config(ExclusiveMonitor& monitor, u32 core = 0, bool absolute = false, u32 stride = 3, u64 address = 0x4080) {
        A64::UserConfig conf{};
        conf.callbacks = this;
        conf.global_monitor = &monitor;
        conf.processor_id = core;
        conf.code_cache_size = 8 * 1024 * 1024;
        conf.page_table = reinterpret_cast<void**>(table.data());
        conf.page_table_exclusive_access = true;
        conf.page_table_address_space_bits = 16;
        conf.page_table_log2_stride = stride;
        conf.page_table_marked_bit = 0;
        conf.page_table_pointer_mask = ~u64{7};
        conf.silently_mirror_page_table = false;
        conf.absolute_offset_page_table = absolute;
        table[(address >> 12) << (stride - 3)] = reinterpret_cast<uintptr_t>(memory + host_offset) - (absolute ? (address & ~u64{0xfff}) : 0);
        ticks_left = 100;
        return conf;
    }

    void Pair(u32 bits, bool ordered = false) {
        oaknut::VectorCodeGenerator code{code_mem, nullptr};
        switch (bits) {
        case 8:
            if (ordered) {
                code.LDAXRB(W1, X0);
                code.STLXRB(W2, W3, X0);
            } else {
                code.LDXRB(W1, X0);
                code.STXRB(W2, W3, X0);
            }
            break;
        case 16:
            if (ordered) {
                code.LDAXRH(W1, X0);
                code.STLXRH(W2, W3, X0);
            } else {
                code.LDXRH(W1, X0);
                code.STXRH(W2, W3, X0);
            }
            break;
        case 32:
            if (ordered) {
                code.LDAXR(W1, X0);
                code.STLXR(W2, W3, X0);
            } else {
                code.LDXR(W1, X0);
                code.STXR(W2, W3, X0);
            }
            break;
        case 64:
            if (ordered) {
                code.LDAXR(X1, X0);
                code.STLXR(W2, X3, X0);
            } else {
                code.LDXR(X1, X0);
                code.STXR(W2, X3, X0);
            }
            break;
        case 128:
            code.LDXP(X1, X2, X0);
            code.STXP(W4, X5, X6, X0);
            break;
        }
    }

    void Initialize(A64::Jit& jit, u64 address = 0x4080) {
        jit.SetPC(0);
        jit.SetRegister(0, address);
        jit.SetRegister(3, 0xfedcba9876543210);
        for (size_t i = 4; i < 31; ++i) {
            jit.SetRegister(i, 0x12340000 + i);
        }
        const u64 original = 0x8877665544332211;
        std::memcpy(Pointer(address), &original, sizeof(original));
    }
};

TEST_CASE("A64 exclusive page-table path bypasses callbacks at all scalar widths",
          "[a64][atomic-fastpath]") {
    for (u32 bits : {8U, 16U, 32U, 64U}) {
        for (bool ordered : {false, true}) {
            for (bool absolute : {false, true}) {
                for (u32 stride : {3U, 4U}) {
                    CAPTURE(bits, ordered, absolute, stride);
                    ExclusiveEnv env;
                    ExclusiveMonitor monitor{1};
                    env.Pair(bits, ordered);
                    auto conf = env.Config(monitor, 0, absolute, stride);
                    env.table[4 << (stride - 3)] |= 4;  // masked non-marking attribute
                    A64::Jit jit{conf};
                    env.Initialize(jit);
                    jit.Step();
                    const u64 mask = bits == 64 ? ~u64{0} : (u64{1} << bits) - 1;
                    REQUIRE(jit.GetRegister(1) == (0x8877665544332211 & mask));
                    jit.Step();
                    REQUIRE(jit.GetRegister(2) == 0);
                    u64 value{};
                    std::memcpy(&value, env.Pointer(0x4080), bits / 8);
                    REQUIRE(value == (0xfedcba9876543210 & mask));
                    REQUIRE(env.reads == 0);
                    REQUIRE(env.writes == 0);
                    for (size_t i = 4; i < 31; ++i) {
                        REQUIRE(jit.GetRegister(i) == 0x12340000 + i);
                    }
                }
            }
        }
    }
}

TEST_CASE("A64 exclusive page-table path retains guarded callbacks", "[a64][atomic-fastpath]") {
    for (u32 scenario = 0; scenario < 8; ++scenario) {
        CAPTURE(scenario);
        ExclusiveEnv env;
        ExclusiveMonitor monitor{1};
        env.Pair(64, true);
        auto conf = env.Config(monitor);
        u64 address = 0x4080;
        switch (scenario) {
        case 0:
            conf.page_table_exclusive_access = false;
            break;
        case 1:
            conf.check_halt_on_memory_access = true;
            break;
        case 2:
            env.table[4] |= 1;
            break;
        case 3:
            env.table[4] = 0;
            break;
        case 4:
            address += 1;
            break;
        case 5:
            address = 0x4fff;
            break;
        case 6:
            address = 0x10080;
            break;
        case 7:
            conf.page_table_pointer_mask = 0;
            conf.page_table_marked_bit.reset();
            env.host_offset = 1;
            env.table[4] += 1;
            break;
        }
        A64::Jit jit{conf};
        env.Initialize(jit, address);
        jit.Step();
        jit.Step();
        REQUIRE(jit.GetRegister(1) == 0x8877665544332211);
        REQUIRE(jit.GetRegister(2) == 0);
        REQUIRE(env.reads == 1);
        REQUIRE(env.writes == 1);
    }
}

TEST_CASE("A64 exclusive path handles marked-page transitions and sign-extended offsets",
          "[a64][atomic-fastpath]") {
    for (bool read_marked : {false, true}) {
        ExclusiveEnv env;
        ExclusiveMonitor monitor{1};
        env.Pair(64);
        auto conf = env.Config(monitor, 0, true);
        const u64 address = 0x7000004080;
        conf.silently_mirror_page_table = true;
        conf.page_table_pointer_mask = 0x00fffffffffff000;
        conf.page_table_sign_extension = 55;
        env.table[4] = (reinterpret_cast<uintptr_t>(env.memory) - (address & ~u64{0xfff})) & conf.page_table_pointer_mask;
        env.table[4] |= read_marked ? 1 : 0;
        A64::Jit jit{conf};
        env.Initialize(jit, address);
        jit.Step();
        env.table[4] ^= 1;
        jit.Step();
        REQUIRE(jit.GetRegister(1) == 0x8877665544332211);
        REQUIRE(jit.GetRegister(2) == 0);
        REQUIRE(env.reads == (read_marked ? 1 : 0));
        REQUIRE(env.writes == (read_marked ? 0 : 1));
    }
}

TEST_CASE("A64 exclusive fast path rejects stale and cleared reservations", "[a64][atomic-fastpath]") {
    for (u32 scenario = 0; scenario < 5; ++scenario) {
        CAPTURE(scenario);
        ExclusiveEnv env;
        ExclusiveMonitor monitor{1};
        env.Pair(64);
        auto conf = env.Config(monitor);
        A64::Jit jit{conf};
        env.Initialize(jit);
        if (scenario != 4) {
            jit.Step();
        }
        switch (scenario) {
        case 0:
            monitor.ClearProcessor(0);
            break;
        case 1:
            jit.ClearExclusiveState();
            break;
        case 2:
            env.memory[0x80] = 0;
            break;
        case 3:
            env.table[4] += 4096;
            break;
        case 4:
            jit.SetPC(4);
            break;
        }
        jit.Step();
        REQUIRE(jit.GetRegister(2) == 1);
        REQUIRE(env.reads == 0);
        REQUIRE(env.writes == 0);
    }
}

TEST_CASE("A64 direct and callback exclusives share global invalidation", "[a64][atomic-fastpath]") {
    for (bool second_direct : {false, true}) {
        ExclusiveEnv first;
        ExclusiveEnv second;
        second.memory = first.memory;
        ExclusiveMonitor monitor{2};
        first.Pair(64);
        second.Pair(64);
        auto first_conf = first.Config(monitor, 0);
        auto second_conf = second.Config(monitor, 1);
        second_conf.page_table_exclusive_access = second_direct;
        A64::Jit a{first_conf};
        A64::Jit b{second_conf};
        first.Initialize(a);
        second.Initialize(b);
        a.Step();
        b.Step();
        a.SetRegister(3, a.GetRegister(1));  // unchanged value still invalidates core 1
        a.Step();
        b.Step();
        REQUIRE(a.GetRegister(2) == 0);
        REQUIRE(b.GetRegister(2) == 1);
        REQUIRE(first.reads == 0);
        REQUIRE(first.writes == 0);
        REQUIRE(second.reads == (second_direct ? 0 : 1));
        REQUIRE(second.writes == 0);
    }
}

TEST_CASE("A64 128-bit exclusive accesses retain callbacks with fast path enabled", "[a64][atomic-fastpath]") {
    ExclusiveEnv env;
    ExclusiveMonitor monitor{1};
    env.Pair(128);
    auto conf = env.Config(monitor);
    A64::Jit jit{conf};
    env.Initialize(jit);
    jit.SetRegister(5, 0x1234);
    jit.SetRegister(6, 0x5678);
    jit.Step();
    jit.Step();
    REQUIRE(jit.GetRegister(4) == 0);
    REQUIRE(env.reads == 1);
    REQUIRE(env.writes == 1);
    Vector result;
    std::memcpy(result.data(), env.Pointer(0x4080), sizeof(result));
    REQUIRE(result == Vector{0x1234, 0x5678});
}

TEST_CASE("A64 exclusive fast path serializes six JIT cores", "[a64][atomic-fastpath]") {
    constexpr size_t cores = 6;
    constexpr u64 iterations = 2000;
    ExclusiveMonitor monitor{cores};
    std::array<std::unique_ptr<ExclusiveEnv>, cores> envs;
    std::array<std::unique_ptr<A64::Jit>, cores> jits;
    for (size_t i = 0; i < cores; ++i) {
        envs[i] = std::make_unique<ExclusiveEnv>();
        auto& env = *envs[i];
        env.memory = envs[0]->memory;
        auto conf = env.Config(monitor, static_cast<u32>(i));
        oaknut::VectorCodeGenerator code{env.code_mem, nullptr};
        oaknut::Label retry, done;
        code.l(retry);
        code.LDAXR(X1, X0);
        code.ADD(X3, X1, 1);
        code.STLXR(W2, X3, X0);
        code.CBNZ(W2, retry);
        code.SUB(X4, X4, 1);
        code.CBNZ(X4, retry);
        code.l(done);
        code.B(done);
        jits[i] = std::make_unique<A64::Jit>(conf);
        jits[i]->SetRegister(0, 0x4080);
        jits[i]->SetRegister(4, iterations);
        env.ticks_left = 1000000;
    }
    std::barrier start{static_cast<std::ptrdiff_t>(cores)};
    std::vector<std::jthread> workers;
    for (size_t i = 0; i < cores; ++i) {
        workers.emplace_back([&, i] {
            start.arrive_and_wait();
            jits[i]->Run();
        });
    }
    workers.clear();
    u64 result;
    std::memcpy(&result, envs[0]->Pointer(0x4080), sizeof(result));
    REQUIRE(result == cores * iterations);
    for (size_t i = 0; i < cores; ++i) {
        REQUIRE(jits[i]->GetRegister(4) == 0);
        REQUIRE(envs[i]->reads == 0);
        REQUIRE(envs[i]->writes == 0);
    }
}

}  // namespace
#endif
