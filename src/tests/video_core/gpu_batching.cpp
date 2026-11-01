// SPDX-License-Identifier: GPL-3.0-or-later

#include <chrono>
#include <filesystem>
#include <numeric>
#include <tuple>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "common/fs/path_util.h"
#include "core/core.h"
#include "core/device_memory.h"
#include "video_core/control/channel_state.h"
#include "video_core/dma_pusher.h"
#include "video_core/engines/engine_interface.h"
#include "video_core/gpu_split.h"
#include "video_core/memory_manager.h"

namespace Tegra {
struct DmaPusherTestAccess {
    static size_t Process(DmaPusher& pusher, std::span<const CommandHeader> words, size_t offset) {
        return pusher.ProcessCommands(words, offset);
    }
};
} // namespace Tegra

namespace VideoCommon {

struct GpuSplitTestAccess {
    GpuSplit storage;
    GpuSplit& queue;
    u64 read_position{};

    explicit GpuSplitTestAccess(Core::System& system, bool wrap = false, GpuSplit* target = nullptr)
        : queue{target != nullptr ? *target : storage} {
        queue.system = &system;
        queue.active = target != nullptr;
        queue.front_engine = nullptr;
        queue.back_engine = nullptr;
        queue.ring = std::make_unique<u64[]>(GpuSplit::RingWords);
        read_position = wrap ? GpuSplit::RingWords - 4 : 0;
        queue.write_position = read_position;
        queue.published_local = read_position;
        queue.consumed_cache = read_position;
        queue.published.store(read_position);
        queue.consumed.Publish(read_position);
    }

    ~GpuSplitTestAccess() {
        queue.active = false;
        queue.front_engine = nullptr;
        queue.back_engine = nullptr;
    }

    void ExecuteAll() {
        queue.Publish();
        const u64 available = queue.published.load(std::memory_order_acquire);
        while (read_position != available) {
            const size_t index = read_position & GpuSplit::RingMask;
            const size_t size = queue.Execute(&queue.ring[index]);
            read_position += size != 0 ? size : GpuSplit::RingWords - index;
        }
        queue.consumed.Publish(read_position);
    }

    u64 Written() const {
        return queue.write_position;
    }
};

} // namespace VideoCommon

namespace {

struct TestSystem {
    const std::filesystem::path old_nand = Common::FS::GetEdenPath(Common::FS::EdenPath::NANDDir);
    const std::filesystem::path temporary =
        std::filesystem::temp_directory_path() /
        ("eglantine-gpu-test-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::unique_ptr<Core::System> system;

    TestSystem() {
        std::filesystem::create_directories(temporary);
        Common::FS::SetEdenPath(Common::FS::EdenPath::NANDDir, temporary);
        system = std::make_unique<Core::System>();
    }

    ~TestSystem() {
        system.reset();
        Common::FS::SetEdenPath(Common::FS::EdenPath::NANDDir, old_nand);
        std::filesystem::remove_all(temporary);
    }
};

class RecordingEngine : public Tegra::Engines::EngineInterface {
public:
    using Event = std::tuple<u32, u32, bool, GPUVAddr>;
    std::vector<Event> events;

    RecordingEngine() {
        current_dma_segment = 0x1234;
        for (size_t i = 0; i < execution_mask.size(); ++i) {
            execution_mask[i] = i % 4 != 0;
        }
    }

    void CallMethod(Core::System&, u32 method, u32 value, bool is_last) override {
        events.emplace_back(method, value, is_last, current_dma_segment);
    }

    void CallMultiMethod(Core::System& system, u32 method, const u32* arguments, u32 count,
                         u32 pending) override {
        for (u32 i = 0; i < count; ++i) {
            CallMethod(system, method, arguments[i], i + 1 == pending);
        }
    }
};

TEST_CASE("Packed GPU methods match scalar side effects, segments and last-call flags",
          "[video_core][gpu-batching]") {
    TestSystem fixture;
    for (const u32 count : {1U, 2U, 5U, 8193U}) {
        for (bool segment : {false, true}) {
            for (bool is_last : {false, true}) {
                for (bool wrap : {false, true}) {
                    CAPTURE(count, segment, is_last, wrap);
                    RecordingEngine scalar_engine;
                    RecordingEngine batch_engine;
                    VideoCommon::GpuSplitTestAccess scalar(*fixture.system, wrap);
                    VideoCommon::GpuSplitTestAccess batch(*fixture.system, wrap);
                    std::vector<u32> arguments(count);
                    std::iota(arguments.begin(), arguments.end(), 0x80000000U);
                    GPUVAddr address = 0x100004;
                    batch.queue.IncreasingMethods(&batch_engine, 0x40, arguments.data(), count,
                                                  is_last, segment ? &address : nullptr);
                    for (u32 i = 0; i < count; ++i) {
                        GPUVAddr current = address + i * sizeof(u32);
                        scalar.queue.Method(&scalar_engine, 0x40 + i, arguments[i],
                                            is_last && i + 1 == count,
                                            segment ? &current : nullptr);
                    }
                    batch.ExecuteAll();
                    scalar.ExecuteAll();
                    batch_engine.ConsumeSink(*fixture.system);
                    scalar_engine.ConsumeSink(*fixture.system);
                    REQUIRE(batch_engine.events == scalar_engine.events);
                    REQUIRE(batch_engine.events.size() == count);
                    if (count >= 5 && !wrap) {
                        REQUIRE(batch.Written() < scalar.Written());
                    }
                }
            }
        }
    }
}

TEST_CASE("Packed GPU methods preserve engine switches and deferred work ordering",
          "[video_core][gpu-batching]") {
    TestSystem fixture;
    VideoCommon::GpuSplitTestAccess batch(*fixture.system);
    RecordingEngine first;
    RecordingEngine second;
    first.execution_mask.set();
    second.execution_mask.set();
    std::array<u32, 3> values{11, 22, 33};
    bool deferred_ran = false;
    batch.queue.IncreasingMethods(&first, 0x40, values.data(), 3, true, nullptr);
    batch.queue.Defer([&first, &second, &deferred_ran] {
        REQUIRE(first.events.size() == 3);
        REQUIRE(second.events.empty());
        deferred_ran = true;
    });
    batch.queue.IncreasingMethods(&second, 0x80, values.data(), 3, false, nullptr);
    // Queued arguments must outlive the caller's scratch command buffer.
    values.fill(0);
    batch.ExecuteAll();
    REQUIRE(deferred_ran);
    REQUIRE(first.events.size() == 3);
    REQUIRE(second.events.size() == 3);
    REQUIRE(std::get<1>(first.events[2]) == 33);
    REQUIRE(std::get<2>(first.events[2]));
    REQUIRE_FALSE(std::get<2>(second.events[2]));
}

TEST_CASE("DMA batching preserves split command lists and submission modes",
          "[video_core][gpu-batching]") {
    TestSystem fixture;
    Core::DeviceMemory physical_memory;
    Tegra::MaxwellDeviceMemoryManager device_memory(physical_memory);
    Tegra::MemoryManager memory(*fixture.system, device_memory, 32, 1ULL << 30);
    Tegra::Control::ChannelState channel(0);
    std::vector<Tegra::CommandHeader> commands;
    const auto append = [&](u32 method, u32 count, Tegra::SubmissionMode mode) {
        commands.push_back(
            Tegra::BuildCommandHeader(static_cast<Tegra::BufferMethods>(method), count, mode));
        for (u32 i = 0; i < count; ++i) {
            commands.push_back(Tegra::CommandHeader{.argument = i + 1});
        }
    };
    append(0x40, 5, Tegra::SubmissionMode::Increasing);
    append(0x80, 4, Tegra::SubmissionMode::IncreaseOnce);
    append(0x90, 3, Tegra::SubmissionMode::NonIncreasing);
    append(0xdfe, 4, Tegra::SubmissionMode::Increasing);
    commands.push_back(Tegra::BuildCommandHeader(static_cast<Tegra::BufferMethods>(0x60), 7,
                                                 Tegra::SubmissionMode::Inline));
    append(2, 1, Tegra::SubmissionMode::Increasing); // puller NOP

    for (const size_t chunk_size : {size_t{2}, size_t{7}, commands.size()}) {
        CAPTURE(chunk_size);
        RecordingEngine scalar_engine;
        RecordingEngine batch_engine;
        Tegra::DmaPusher scalar(*fixture.system, memory, channel);
        Tegra::DmaPusher batched(*fixture.system, memory, channel);
        scalar.BindSubchannel(&scalar_engine, 0, Tegra::Engines::EngineTypes::KeplerCompute);
        batched.BindSubchannel(&batch_engine, 0, Tegra::Engines::EngineTypes::KeplerCompute);
        scalar.dma_state.dma_get = batched.dma_state.dma_get = 0x100000;
        for (size_t i = 0; i < commands.size(); i += chunk_size) {
            const auto words =
                std::span{commands}.subspan(i, std::min(chunk_size, commands.size() - i));
            REQUIRE(Tegra::DmaPusherTestAccess::Process(scalar, words, i) == words.size());
        }
        scalar_engine.ConsumeSink(*fixture.system);
        {
            VideoCommon::GpuSplitTestAccess queue(*fixture.system, false,
                                                  &VideoCommon::GpuSplit::Get());
            for (size_t i = 0; i < commands.size(); i += chunk_size) {
                const auto words =
                    std::span{commands}.subspan(i, std::min(chunk_size, commands.size() - i));
                REQUIRE(Tegra::DmaPusherTestAccess::Process(batched, words, i) == words.size());
            }
            queue.ExecuteAll();
            batch_engine.ConsumeSink(*fixture.system);
        }
        REQUIRE(batch_engine.events == scalar_engine.events);
        REQUIRE(batched.dma_state.method == scalar.dma_state.method);
        REQUIRE(batched.dma_state.method_count == scalar.dma_state.method_count);
        REQUIRE(batched.dma_state.is_last_call == scalar.dma_state.is_last_call);
        REQUIRE(batched.dma_state.dma_word_offset == scalar.dma_state.dma_word_offset);
    }
}

} // namespace
