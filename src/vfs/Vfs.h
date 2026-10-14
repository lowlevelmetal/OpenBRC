#pragma once

#include "vfs/FileSystem.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace brc::vfs {

// Ordered stack of mounted file systems. When several provide the same path,
// the mount with the highest priority wins; among equal priorities the most
// recently mounted wins (so later archives override earlier ones).
class Vfs {
public:
    void mount(std::shared_ptr<const FileSystem> fs, int priority = 0);
    void clear();
    std::size_t mountCount() const { return m_mounts.size(); }

    std::shared_ptr<RandomAccessFile> open(std::string_view path) const;
    std::optional<std::vector<std::byte>> readAll(std::string_view path) const;
    bool exists(std::string_view path) const;

    // The file system that would satisfy open(path), or nullptr.
    const FileSystem* resolve(std::string_view path) const;

    // Union of all files, each path reported once (from its winning mount).
    std::vector<EntryInfo> listFiles() const;

private:
    struct Mount {
        std::shared_ptr<const FileSystem> fs;
        int priority;
        std::uint64_t order;
    };
    std::vector<Mount> m_mounts; // sorted: highest precedence first
    std::uint64_t m_nextOrder = 0;
};

} // namespace brc::vfs
