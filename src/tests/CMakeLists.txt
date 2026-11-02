# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

# SPDX-FileCopyrightText: 2018 yuzu Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later

add_executable(tests
    common/apple_memory.cpp
    common/bit_field.cpp
    common/cityhash.cpp
    common/container_hash.cpp
    common/fibers.cpp
    common/host_memory.cpp
    common/param_package.cpp
    common/profile_paths.cpp
    common/range_map.cpp
    common/ring_buffer.cpp
    common/scratch_buffer.cpp
    common/unique_function.cpp
    core/file_sys/vfs_real.cpp
    core/software_keyboard.cpp
    core/audio_out.cpp
    core/play_time_manager.cpp
    video_core/frame_trace.cpp
    video_core/draw_diagnostics.cpp
    video_core/shader_notify.cpp
    video_core/syncpoint_manager.cpp
    video_core/command_buffer.cpp
    video_core/texture_streaming.cpp
    core/memory_span.cpp
    core/core_timing.cpp
    core/internal_network/network.cpp
    video_core/memory_tracker.cpp
    video_core/gpu_queue_progress.cpp
    video_core/gpu_batching.cpp
    video_core/shader_reuse.cpp
    input_common/sdl_events.cpp
    video_core/descriptor_reuse.cpp
    video_core/swapchain_acquire.cpp
    video_core/ampere_returns.cpp
    video_core/ampere_branch.cpp
    video_core/ampere_texture.cpp
    video_core/shared_memory.cpp
    video_core/shared_memory_audit.cpp
    video_core/ampere_round.cpp
    video_core/ampere_select.cpp
    video_core/ampere_surface.cpp
    video_core/ampere_uniform.cpp
    video_core/pipeline_cache_file.cpp
    video_core/pipeline_cache_audit.cpp
    video_core/vulkan_device_smoke.cpp
    video_core/astc_empty_blocks.cpp
    input_common/calibration_configuration_job.cpp
)

create_target_directory_groups(tests)

target_link_libraries(tests PRIVATE common core frontend_common input_common video_core)
target_link_libraries(tests PRIVATE ${PLATFORM_LIBRARIES} Catch2::Catch2WithMain Threads::Threads)

add_test(NAME tests COMMAND tests)

# needed for vma
if (NOT MSVC)
    target_compile_options(tests PRIVATE
        $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-conversion>
        $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-unused-variable>
        $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-unused-parameter>
        $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-missing-field-initializers>)
endif()
