#include "vfs/DirectoryFs.h"

#include "core/Log.h"
#include "core/StringUtil.h"

namespace brc::vfs {

DirectoryFs::DirectoryFs(std::filesystem::path root, int maxDepth)
    : m_root(std::move(root)), m_maxDepth(maxDepth) {
    rescan();
}

void DirectoryFs::rescan() {
    m_files.clear();
    std::error_code ec;
    auto it = std::filesystem::recursive_directory_iterator(
        m_root, std::filesystem::directory_options::skip_permission_denied, ec);
    if (ec) {
        log::warn("vfs: cannot scan '{}': {}", str::fromPath(m_root), ec.message());
        return;
    }
    for (; it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (ec)
            break;
        if (it.depth() >= m_maxDepth)
            it.disable_recursion_pending();
        if (!it->is_regular_file(ec))
            continue;
        const auto rel = std::filesystem::relative(it->path(), m_root, ec);
        if (ec)
            continue;
        const std::string key = str::normalizeVirtualPath(str::fromPath(rel));
        // On case-sensitive hosts two files may differ only by case; keep the first.
        m_files.try_emplace(key, Entry{it->path(), it->file_size(ec)});
    }
}

std::string DirectoryFs::describe() const { return "directory " + str::fromPath(m_root); }

std::optional<std::filesystem::path> DirectoryFs::hostPath(std::string_view path) const {
    const auto it = m_files.find(str::normalizeVirtualPath(path));
    if (it == m_files.end())
        return std::nullopt;
    return it->second.hostPath;
}

std::shared_ptr<RandomAccessFile> DirectoryFs::open(std::string_view path) const {
    const auto host = hostPath(path);
    if (!host)
        return nullptr;
    return OsFile::open(*host);
}

bool DirectoryFs::exists(std::string_view path) const {
    return m_files.contains(str::normalizeVirtualPath(path));
}

void DirectoryFs::forEachFile(const std::function<void(const EntryInfo&)>& fn) const {
    for (const auto& [key, e] : m_files)
        fn(EntryInfo{key, e.size, false});
}

} // namespace brc::vfs
