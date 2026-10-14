#include "vfs/Vfs.h"

#include "core/StringUtil.h"

#include <algorithm>
#include <unordered_set>

namespace brc::vfs {

void Vfs::mount(std::shared_ptr<const FileSystem> fs, int priority) {
    m_mounts.push_back({std::move(fs), priority, m_nextOrder++});
    std::ranges::stable_sort(m_mounts, [](const Mount& a, const Mount& b) {
        if (a.priority != b.priority)
            return a.priority > b.priority;
        return a.order > b.order;
    });
}

void Vfs::clear() { m_mounts.clear(); }

const FileSystem* Vfs::resolve(std::string_view path) const {
    const std::string norm = str::normalizeVirtualPath(path);
    for (const auto& m : m_mounts)
        if (m.fs->exists(norm))
            return m.fs.get();
    return nullptr;
}

std::shared_ptr<RandomAccessFile> Vfs::open(std::string_view path) const {
    const std::string norm = str::normalizeVirtualPath(path);
    for (const auto& m : m_mounts)
        if (auto f = m.fs->open(norm))
            return f;
    return nullptr;
}

std::optional<std::vector<std::byte>> Vfs::readAll(std::string_view path) const {
    auto f = open(path);
    if (!f)
        return std::nullopt;
    auto data = f->readAll();
    if (data.size() != f->size())
        return std::nullopt;
    return data;
}

bool Vfs::exists(std::string_view path) const { return resolve(path) != nullptr; }

std::vector<EntryInfo> Vfs::listFiles() const {
    std::vector<EntryInfo> out;
    std::unordered_set<std::string> seen;
    for (const auto& m : m_mounts) {
        m.fs->forEachFile([&](const EntryInfo& e) {
            if (seen.insert(e.path).second)
                out.push_back(e);
        });
    }
    std::ranges::sort(out, {}, &EntryInfo::path);
    return out;
}

} // namespace brc::vfs
