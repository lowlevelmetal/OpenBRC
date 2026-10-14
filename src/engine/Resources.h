#pragma once

#include "engine/Image8.h"
#include "formats/Canvas.h"
#include "formats/HcAnim.h"
#include "formats/OmtFile.h"
#include "vfs/Vfs.h"

#include <array>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace brc::engine {

// The application's list of open resource databases (HCApplication +0xd4):
// .omt files searched in list order for chunks by type and name. Files are
// reference counted and only freed by purge(), as in 0x44d9c0/0x44dbb0. The
// original opened each file from both the HD and the CD path; the mounted
// game data already prefers an installation's copy, so each file is open once.
class Resources {
public:
    explicit Resources(const vfs::Vfs& data) : m_data(data) {}

    // 0x44d510(name, prepend, cache = 0): opens "Resources/<name>" (adding a
    // reference if it is already open). Returns false when the file is missing.
    bool open(std::string_view fileName, bool prepend = false);
    void close(std::string_view fileName);
    // Deletes every file whose reference count dropped below 1.
    void purge();
    void closeAll();
    bool isOpen(std::string_view fileName) const;
    std::vector<std::string> openFiles() const;

    struct Found {
        const formats::OmtFile* file = nullptr;
        const formats::OmtChunk* chunk = nullptr;
        explicit operator bool() const { return chunk != nullptr; }
    };
    // 0x44dca0: the first open file with a chunk of `type` named `name`
    // (case-insensitive).
    Found find(formats::FourCC type, std::string_view name) const;
    std::vector<std::byte> read(const Found& f) const;
    std::shared_ptr<RandomAccessFile> openChunk(const Found& f) const;

    // 0x44dd90(name, preferBlit = 1): a still image, the BLIT if there is one
    // (raw display indices), else the Canv, remapped to `display` when its
    // own palette differs (canvas.md §6). Cached per chunk until purge().
    std::shared_ptr<const Image8> image(std::string_view name, const formats::Palette& display);
    // The palette of the canvas called `name` (HCNode reads its background's).
    std::optional<formats::Palette> canvasPalette(std::string_view name) const;
    // A canvas with its own pixels (fonts: 32-bit glyph strips).
    std::optional<formats::Canvas> canvas(std::string_view name) const;

    // A ".omt.cache" file (HD path first, then the CD path in the original).
    std::optional<formats::AnimCache> openCache(std::string_view fileName) const;

    // Any game file, e.g. "Resources/SMK/Bridge.smk".
    std::shared_ptr<RandomAccessFile> openFile(std::string_view path) const;

private:
    struct OpenFile {
        std::string name; // lower case
        int refs = 0;
        std::unique_ptr<formats::OmtFile> omt;
    };
    OpenFile* findOpen(std::string_view fileName);
    const OpenFile* findOpen(std::string_view fileName) const;

    const vfs::Vfs& m_data;
    std::list<OpenFile> m_files; // search order
    std::map<std::pair<const formats::OmtChunk*, std::array<std::uint32_t, 256>>, std::shared_ptr<const Image8>> m_images;
};

std::string resourcePath(std::string_view fileName);

} // namespace brc::engine
