#include "core/File.h"

#include "core/StringUtil.h"

#include <format>
#include <fstream>
#include <random>

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace brc {

std::vector<std::byte> RandomAccessFile::readAll() const {
    std::vector<std::byte> out(static_cast<std::size_t>(size()));
    out.resize(readAt(0, out));
    return out;
}

// --- OsFile -----------------------------------------------------------------

#ifdef _WIN32

std::unique_ptr<OsFile> OsFile::open(const std::filesystem::path& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return nullptr;
    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(h, &sz)) {
        CloseHandle(h);
        return nullptr;
    }
    std::unique_ptr<OsFile> f(new OsFile());
    f->m_handle = h;
    f->m_size = static_cast<std::uint64_t>(sz.QuadPart);
    return f;
}

OsFile::~OsFile() {
    if (m_handle)
        CloseHandle(static_cast<HANDLE>(m_handle));
}

std::size_t OsFile::readAt(std::uint64_t offset, std::span<std::byte> out) const {
    std::size_t total = 0;
    while (total < out.size()) {
        OVERLAPPED ov{};
        const std::uint64_t pos = offset + total;
        ov.Offset = static_cast<DWORD>(pos & 0xFFFFFFFFu);
        ov.OffsetHigh = static_cast<DWORD>(pos >> 32);
        const DWORD want = static_cast<DWORD>(std::min<std::size_t>(out.size() - total, 1u << 30));
        DWORD got = 0;
        if (!ReadFile(static_cast<HANDLE>(m_handle), out.data() + total, want, &got, &ov) || got == 0)
            break;
        total += got;
    }
    return total;
}

#else

std::unique_ptr<OsFile> OsFile::open(const std::filesystem::path& path) {
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return nullptr;
    struct stat st{};
    if (::fstat(fd, &st) != 0) {
        ::close(fd);
        return nullptr;
    }
    std::uint64_t size = static_cast<std::uint64_t>(st.st_size);
    if (S_ISBLK(st.st_mode)) {
        // Optical drives and other block devices report st_size == 0.
        const off_t end = ::lseek(fd, 0, SEEK_END);
        size = end > 0 ? static_cast<std::uint64_t>(end) : 0;
    } else if (!S_ISREG(st.st_mode)) {
        ::close(fd);
        return nullptr;
    }
    std::unique_ptr<OsFile> f(new OsFile());
    f->m_fd = fd;
    f->m_size = size;
    return f;
}

OsFile::~OsFile() {
    if (m_fd >= 0)
        ::close(m_fd);
}

std::size_t OsFile::readAt(std::uint64_t offset, std::span<std::byte> out) const {
    std::size_t total = 0;
    while (total < out.size()) {
        const ssize_t got =
            ::pread(m_fd, out.data() + total, out.size() - total, static_cast<off_t>(offset + total));
        if (got < 0 && errno == EINTR)
            continue;
        if (got <= 0)
            break;
        total += static_cast<std::size_t>(got);
    }
    return total;
}

#endif

// --- MemoryFile / SubFile ----------------------------------------------------

std::size_t MemoryFile::readAt(std::uint64_t offset, std::span<std::byte> out) const {
    if (offset >= m_data.size())
        return 0;
    const std::size_t n = std::min<std::size_t>(out.size(), m_data.size() - static_cast<std::size_t>(offset));
    std::memcpy(out.data(), m_data.data() + offset, n);
    return n;
}

std::size_t SubFile::readAt(std::uint64_t offset, std::span<std::byte> out) const {
    if (offset >= m_size)
        return 0;
    const std::size_t n = static_cast<std::size_t>(std::min<std::uint64_t>(out.size(), m_size - offset));
    return m_parent->readAt(m_offset + offset, out.first(n));
}

// --- Helpers -----------------------------------------------------------------

namespace file {

std::optional<std::vector<std::byte>> readBinary(const std::filesystem::path& path) {
    auto f = OsFile::open(path);
    if (!f)
        return std::nullopt;
    auto data = f->readAll();
    if (data.size() != f->size())
        return std::nullopt;
    return data;
}

std::optional<std::string> readText(const std::filesystem::path& path) {
    auto data = readBinary(path);
    if (!data)
        return std::nullopt;
    return std::string(reinterpret_cast<const char*>(data->data()), data->size());
}

bool writeAtomic(const std::filesystem::path& path, std::span<const std::byte> contents) {
    std::error_code ec;
    if (path.has_parent_path())
        std::filesystem::create_directories(path.parent_path(), ec);

    std::random_device rd;
    auto tmp = path;
    tmp += std::format(".{:08x}.tmp", rd());
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out)
            return false;
        out.write(reinterpret_cast<const char*>(contents.data()), static_cast<std::streamsize>(contents.size()));
        if (!out.good()) {
            out.close();
            std::filesystem::remove(tmp, ec);
            return false;
        }
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        return false;
    }
    return true;
}

bool writeAtomic(const std::filesystem::path& path, std::string_view contents) {
    return writeAtomic(path, std::as_bytes(std::span(contents.data(), contents.size())));
}

} // namespace file
} // namespace brc
