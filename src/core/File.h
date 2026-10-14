#pragma once

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace brc {

// Random-access read-only byte source. Implementations must be safe to call
// readAt() from multiple threads concurrently (no shared file cursor).
class RandomAccessFile {
public:
    virtual ~RandomAccessFile() = default;

    virtual std::uint64_t size() const = 0;

    // Reads up to `out.size()` bytes at `offset`. Returns the number of bytes
    // read, which is less than requested only at end of file or on error.
    virtual std::size_t readAt(std::uint64_t offset, std::span<std::byte> out) const = 0;

    bool readExact(std::uint64_t offset, std::span<std::byte> out) const {
        return readAt(offset, out) == out.size();
    }
    std::vector<std::byte> readAll() const;
};

// File on the host filesystem.
class OsFile final : public RandomAccessFile {
public:
    static std::unique_ptr<OsFile> open(const std::filesystem::path& path);
    ~OsFile() override;

    std::uint64_t size() const override { return m_size; }
    std::size_t readAt(std::uint64_t offset, std::span<std::byte> out) const override;

private:
    OsFile() = default;
#ifdef _WIN32
    void* m_handle = nullptr;
#else
    int m_fd = -1;
#endif
    std::uint64_t m_size = 0;
};

// In-memory file (e.g. a decompressed archive entry).
class MemoryFile final : public RandomAccessFile {
public:
    explicit MemoryFile(std::vector<std::byte> data) : m_data(std::move(data)) {}

    std::uint64_t size() const override { return m_data.size(); }
    std::size_t readAt(std::uint64_t offset, std::span<std::byte> out) const override;
    std::span<const std::byte> data() const { return m_data; }

private:
    std::vector<std::byte> m_data;
};

// Window [offset, offset + size) of another file.
class SubFile final : public RandomAccessFile {
public:
    SubFile(std::shared_ptr<const RandomAccessFile> parent, std::uint64_t offset, std::uint64_t size)
        : m_parent(std::move(parent)), m_offset(offset), m_size(size) {}

    std::uint64_t size() const override { return m_size; }
    std::size_t readAt(std::uint64_t offset, std::span<std::byte> out) const override;

private:
    std::shared_ptr<const RandomAccessFile> m_parent;
    std::uint64_t m_offset;
    std::uint64_t m_size;
};

namespace file {

std::optional<std::vector<std::byte>> readBinary(const std::filesystem::path& path);
std::optional<std::string> readText(const std::filesystem::path& path);

// Writes via a temporary file and rename so a crash never leaves a truncated file.
bool writeAtomic(const std::filesystem::path& path, std::string_view contents);
bool writeAtomic(const std::filesystem::path& path, std::span<const std::byte> contents);

} // namespace file

// Little-endian helpers for parsing original game formats (all x86 LE).
template <class T>
T loadLE(const std::byte* p) {
    static_assert(std::is_trivially_copyable_v<T>);
    T v;
    std::memcpy(&v, p, sizeof(T));
    if constexpr (std::endian::native == std::endian::big && sizeof(T) > 1) {
        auto* b = reinterpret_cast<unsigned char*>(&v);
        std::reverse(b, b + sizeof(T));
    }
    return v;
}

} // namespace brc
