// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <atomic>
#include <barrier>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>
#include <thread>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include "common/fs/file.h"
#include "core/file_sys/vfs/vfs_real.h"

namespace {
struct TemporaryDirectory {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("eglantine-vfs-test-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    TemporaryDirectory() {
        std::filesystem::create_directories(path);
    }
    ~TemporaryDirectory() {
        std::filesystem::remove_all(path);
    }
};

u8 Pattern(size_t file, size_t offset) {
    return static_cast<u8>((offset * 17 + (offset >> 8) + file * 37) & 255);
}

TEST_CASE("Concurrent VFS reads preserve offsets through handle eviction", "[vfs-streaming]") {
    TemporaryDirectory temp;
    FileSys::RealVfsFilesystem fs;
    constexpr size_t file_count = 140;
    constexpr size_t file_size = 32768;
    std::vector<FileSys::VirtualFile> files;
    for (size_t index = 0; index < file_count; ++index) {
        auto path = temp.path / std::to_string(index);
        std::vector<u8> contents(file_size);
        for (size_t i = 0; i < contents.size(); ++i)
            contents[i] = Pattern(index, i);
        {
            std::ofstream stream(path, std::ios::binary);
            stream.write(reinterpret_cast<const char*>(contents.data()), contents.size());
            REQUIRE(stream.good());
        }
        files.push_back(fs.OpenFile(path.string()));
        REQUIRE(files.back() != nullptr);
    }
    std::atomic<size_t> errors{};
    std::barrier start{8};
    std::vector<std::jthread> threads;
    for (size_t worker = 0; worker < 8; ++worker) {
        threads.emplace_back([&, worker] {
            start.arrive_and_wait();
            std::array<u8, 4096> bytes;
            for (size_t iteration = 0; iteration < 1200; ++iteration) {
                const size_t index = (iteration * 13 + worker * 7) % file_count;
                const size_t offset = (iteration * 113 + worker * 997) % (file_size - bytes.size());
                if (files[index]->Read(bytes.data(), bytes.size(), offset) != bytes.size()) {
                    ++errors;
                    continue;
                }
                for (size_t i = 0; i < bytes.size(); ++i) {
                    if (bytes[i] != Pattern(index, offset + i)) {
                        ++errors;
                        break;
                    }
                }
            }
        });
    }
    threads.clear();
    REQUIRE(errors == 0);
    std::array<u8, 32> tail{};
    REQUIRE(files[0]->Read(tail.data(), tail.size(), file_size - 7) == 7);
    REQUIRE(files[0]->Read(tail.data(), tail.size(), file_size + 1) == 0);
    REQUIRE(files[0]->Read(tail.data(), tail.size(), std::numeric_limits<size_t>::max()) == 0);
    REQUIRE(files[0]->Read(tail.data(), tail.size(), 0) == tail.size());
    REQUIRE(tail[0] == Pattern(0, 0));
}

TEST_CASE("Concurrent VFS writers retain per-file seek and size consistency", "[vfs-streaming]") {
    TemporaryDirectory temp;
    FileSys::RealVfsFilesystem fs;
    auto file = fs.CreateFile((temp.path / "writable").string());
    REQUIRE(file != nullptr);
    REQUIRE(file->Resize(32768));
    std::atomic<size_t> errors{};
    std::vector<std::jthread> threads;
    for (size_t worker = 0; worker < 8; ++worker) {
        threads.emplace_back([&, worker] {
            std::array<u8, 4096> bytes;
            bytes.fill(static_cast<u8>(worker + 1));
            for (size_t i = 0; i < 100; ++i) {
                if (file->Write(bytes.data(), bytes.size(), worker * bytes.size()) != bytes.size())
                    ++errors;
                if (file->GetSize() != 32768)
                    ++errors;
            }
        });
    }
    threads.clear();
    REQUIRE(errors == 0);
    std::array<u8, 4096> bytes;
    for (size_t worker = 0; worker < 8; ++worker) {
        REQUIRE(file->Read(bytes.data(), bytes.size(), worker * bytes.size()) == bytes.size());
        REQUIRE(std::ranges::all_of(bytes, [worker](u8 value) { return value == worker + 1; }));
    }
    REQUIRE(file->Resize(17));
    REQUIRE(file->GetSize() == 17);
    REQUIRE(file->Read(bytes.data(), bytes.size(), 0) == 17);
}
} // namespace

#ifndef _WIN32
TEST_CASE("Positional reads preserve the stream cursor and reject buffered writable handles",
          "[vfs-streaming]") {
    TemporaryDirectory temp;
    const auto path = temp.path / "position";
    {
        std::ofstream out(path, std::ios::binary);
        out << "0123456789";
    }
    Common::FS::IOFile reader(path, Common::FS::FileAccessMode::Read);
    REQUIRE(reader.Seek(3));
    std::array<u8, 4> data{};
    REQUIRE(reader.ReadAt(data, 7) == 3);
    REQUIRE(data[0] == '7');
    REQUIRE(data[2] == '9');
    REQUIRE(reader.Tell() == 3);
    REQUIRE(reader.ReadAt(data, std::numeric_limits<u64>::max()) == 0);
    REQUIRE(reader.ReadAt(data, 0) == 4);
    REQUIRE(data[0] == '0');
    REQUIRE(reader.Tell() == 3);
    Common::FS::IOFile writer(path, Common::FS::FileAccessMode::ReadWrite);
    REQUIRE(writer.ReadAt(data, 0) == 0);
}
#endif
