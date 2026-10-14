#pragma once

#include "vfs/FileSystem.h"

#include <filesystem>
#include <unordered_map>

namespace brc::vfs {

// Host directory exposed with case-insensitive lookups. The tree is indexed
// once at construction; call rescan() if files are added later.
class DirectoryFs final : public FileSystem {
public:
    explicit DirectoryFs(std::filesystem::path root, int maxDepth = 8);

    const std::filesystem::path& root() const { return m_root; }
    void rescan();

    std::string describe() const override;
    std::shared_ptr<RandomAccessFile> open(std::string_view path) const override;
    bool exists(std::string_view path) const override;
    void forEachFile(const std::function<void(const EntryInfo&)>& fn) const override;

    // Host path for a virtual path, if the file exists.
    std::optional<std::filesystem::path> hostPath(std::string_view path) const;

private:
    struct Entry {
        std::filesystem::path hostPath;
        std::uint64_t size;
    };
    std::filesystem::path m_root;
    int m_maxDepth;
    std::unordered_map<std::string, Entry> m_files;
};

} // namespace brc::vfs
