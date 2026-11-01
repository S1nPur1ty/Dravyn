// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <atomic>
#include <barrier>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include "dynarmic/interface/exclusive_monitor.h"

TEST_CASE("Exclusive monitor preserves reservations and invalidation", "[exclusive]") {
    Dynarmic::ExclusiveMonitor monitor{6};
    std::atomic<std::uint64_t> memory{42};
    const auto read = [&](size_t core, std::uint64_t address) {
        return monitor.ReadAndMark<std::uint64_t>(core, address, [&] { return memory.load(); });
    };
    const auto write = [&](size_t core, std::uint64_t address, std::uint64_t value) {
        return monitor.DoExclusiveOperation<std::uint64_t>(core, address, [&](auto expected) {
            return memory.compare_exchange_strong(expected, value);
        });
    };
    REQUIRE_FALSE(write(0, 0x1000, 1));
    REQUIRE(read(0, 0x1000) == 42);
    REQUIRE(read(1, 0x1000) == 42);
    REQUIRE(write(0, 0x1000, 43));
    REQUIRE_FALSE(write(1, 0x1000, 44));
    REQUIRE_FALSE(write(0, 0x1000, 44));

    read(0, 0x1000);
    memory.store(45);
    REQUIRE_FALSE(write(0, 0x1000, 46));
    REQUIRE(memory.load() == 45);
    read(0, 0x1000);
    read(1, 0x2000);
    monitor.ClearProcessor(0);
    REQUIRE_FALSE(write(0, 0x1000, 46));
    REQUIRE(write(1, 0x2000, 46));
    read(0, 0x1000);
    read(1, 0x2000);
    monitor.Clear();
    REQUIRE_FALSE(write(0, 0x1000, 47));
    REQUIRE_FALSE(write(1, 0x2000, 47));
}

TEST_CASE("Exclusive monitor preserves all value widths", "[exclusive]") {
    Dynarmic::ExclusiveMonitor monitor{6};
    const auto check = [&]<typename T>(T value) {
        REQUIRE(monitor.ReadAndMark<T>(0, 0x1000, [&] { return value; }) == value);
        bool called = false;
        REQUIRE(monitor.DoExclusiveOperation<T>(0, 0x1000, [&](T saved) {
            called = true;
            return saved == value;
        }));
        REQUIRE(called);
    };
    check(std::uint8_t{0xa5});
    check(std::uint16_t{0xa55a});
    check(std::uint32_t{0xa55aff00});
    check(std::uint64_t{0xa55aff0011223344});
    check(Dynarmic::Vector{0x1122334455667788, 0x8877665544332211});

    monitor.ReadAndMark<std::uint64_t>(0, 0x1000, [] { return 0ULL; });
    monitor.ReadAndMark<std::uint8_t>(1, 0x1000, [] { return 0; });
    REQUIRE(monitor.DoExclusiveOperation<std::uint8_t>(1, 0x1000, [](auto) { return true; }));
    REQUIRE_FALSE(monitor.DoExclusiveOperation<std::uint64_t>(0, 0x1000, [](auto) { return true; }));
}

TEST_CASE("Exclusive monitor serializes six guest cores", "[exclusive]") {
    constexpr size_t cores = 6;
    constexpr size_t iterations = 10000;
    Dynarmic::ExclusiveMonitor monitor{cores};
    std::array<std::atomic<std::uint64_t>, cores> memory{};
    std::barrier start{static_cast<std::ptrdiff_t>(cores)};
    std::vector<std::jthread> threads;
    const bool contended = GENERATE(false, true);
    for (size_t core = 0; core < cores; ++core) {
        threads.emplace_back([&, core] {
            const size_t index = contended ? 0 : core;
            const std::uint64_t address = 0x1000 + index * 128;
            start.arrive_and_wait();
            for (size_t i = 0; i < iterations; ++i) {
                bool success = false;
                while (!success) {
                    const auto value = monitor.ReadAndMark<std::uint64_t>(core, address, [&] {
                        return memory[index].load();
                    });
                    // Simulate a context switch between the load/store pair.
                    if (i % 127 == 0) {
                        monitor.ClearProcessor(core);
                        monitor.ReadAndMark<std::uint64_t>(core, address, [&] { return memory[index].load(); });
                    }
                    success = monitor.DoExclusiveOperation<std::uint64_t>(core, address, [&](auto expected) {
                        // Retry if the second reservation observed a newer value.
                        return expected == value && memory[index].compare_exchange_strong(expected, value + 1);
                    });
                }
            }
        });
    }
    threads.clear();
    for (size_t core = 0; core < cores; ++core) {
        REQUIRE(memory[core].load() == (contended ? (core == 0 ? cores * iterations : 0) : iterations));
    }
}
