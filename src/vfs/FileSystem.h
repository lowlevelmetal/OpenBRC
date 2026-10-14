#pragma once

#include "core/File.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace brc::vfs {

struct EntryInfo {
    std::string path; // normalized virtual path (see str::normalizeVirtualPath)
    std::uint64_t size = 0;
    bool isDirectory = false;
};

// A read-only tree of files addressed by normalized virtual paths
// (lowercase, '/' separated, no leading slash). Lookups are case-insensitive.
class FileSystem {
public:
    virtual ~FileSystem() = default;

    // Human-readable description for logs and the setup UI.
    virtual std::string describe() const = 0;

    virtual std::shared_ptr<RandomAccessFile> open(std::string_view path) const = 0;
    virtual bool exists(std::string_view path) const = 0;

    // Calls `fn` for every file (not directory) in the tree.
    virtual void forEachFile(const std::function<void(const EntryInfo&)>& fn) const = 0;
};

} // namespace brc::vfs
