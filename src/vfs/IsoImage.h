#pragma once

#include "vfs/FileSystem.h"

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>

namespace brc::vfs {

// Read-only ISO 9660 file system (with Joliet long names when present).
//
// Accepts plain 2048-byte-sector images (.iso), raw 2352-byte Mode 1 /
// Mode 2 Form 1 images (.bin/.img, e.g. Redump dumps referenced by a .cue),
// 2448-byte raw+subchannel images, and optical block devices.
class IsoImage final : public FileSystem {
public:
    enum class SectorFormat { Cooked2048, RawMode1, RawMode2Form1, RawMode1Sub, RawMode2Form1Sub };

    // Opens an image file or device. If `path` is a .cue sheet, the first
    // data track's file is opened instead. Returns nullptr (and sets `error`)
    // when the file is not a readable ISO 9660 volume.
    static std::shared_ptr<IsoImage> open(const std::filesystem::path& path, std::string* error = nullptr);
    static std::shared_ptr<IsoImage> open(std::shared_ptr<const RandomAccessFile> image, std::string label,
                                          std::string* error = nullptr);

    std::string describe() const override;
    std::shared_ptr<RandomAccessFile> open(std::string_view path) const override;
    bool exists(std::string_view path) const override;
    void forEachFile(const std::function<void(const EntryInfo&)>& fn) const override;

    const std::string& volumeId() const { return m_volumeId; }
    SectorFormat sectorFormat() const { return m_format; }
    bool hasJoliet() const { return m_joliet; }

private:
    struct FileEntry {
        std::uint32_t lba;
        std::uint64_t size;
    };

    IsoImage() = default;
    bool load(std::string* error);
    bool readDirectory(std::uint32_t lba, std::uint32_t size, const std::string& prefix, bool joliet, int depth);

    std::shared_ptr<const RandomAccessFile> m_raw;     // the image as stored
    std::shared_ptr<const RandomAccessFile> m_logical; // 2048-byte logical sectors
    std::string m_label;
    std::string m_volumeId;
    SectorFormat m_format = SectorFormat::Cooked2048;
    bool m_joliet = false;
    std::unordered_map<std::string, FileEntry> m_files;
};

} // namespace brc::vfs
