// The toolbar camera's photo store (BarbieCamera, 0x403f20–0x404f6f). See
// game/Camera.h and docs/formats/savegame.md §3.
#include "game/Camera.h"

#include "core/File.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "core/StringUtil.h"
#include "engine/Node.h"
#include "formats/OmtFile.h"
#include "formats/OmtWriter.h"
#include "game/Godiva.h"

#include <array>
#include <format>
#include <optional>

namespace brc::game {
namespace {

// The database of the current slot ([0x809ac0]), or nothing when it does not
// exist or is not an OMT container.
std::optional<formats::OmtFile> openDatabase(const std::filesystem::path& path) {
    auto data = file::readBinary(path);
    if (!data || data->empty())
        return std::nullopt;
    auto omt = formats::OmtFile::open(std::make_shared<MemoryFile>(std::move(*data)), str::fromPath(path));
    if (!omt) {
        log::warn("BarbieCamera: {}", omt.error());
        return std::nullopt;
    }
    return std::move(*omt);
}

bool writeDatabase(const std::filesystem::path& path, const formats::OmtWriter& db) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (!file::writeAtomic(path, db.serialize())) {
        log::warn("BarbieCamera: cannot write {}", str::fromPath(path));
        return false;
    }
    return true;
}

// Reads photo `id` and converts it to the display palette (the canvas reader
// 0x471e60 remaps 8-bit canvases whose palette differs, canvas.md §6).
std::shared_ptr<engine::Image8> readPhoto(Godiva& app, int id, const formats::Palette& display) {
    const auto db = openDatabase(BarbieCamera::photoFile(app.saveSlot()));
    if (!db)
        return nullptr;
    const formats::OmtChunk* chunk = db->find(formats::chunk::kCanvas, id);
    if (!chunk)
        return nullptr;
    auto cv = formats::parseCanvas(db->read(*chunk));
    if (!cv || cv->depth != 8 || cv->indices.empty()) {
        log::warn("BarbieCamera: photo {}: {}", id, cv ? "not an 8-bit canvas" : cv.error());
        return nullptr;
    }
    auto img = std::make_shared<engine::Image8>(cv->width, cv->height);
    std::array<std::uint8_t, 256> map{};
    for (int i = 0; i < 256; ++i)
        map[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(i);
    if (cv->palette && !(cv->palette->rgb == display.rgb))
        for (int i = 0; i < 256; ++i)
            map[static_cast<std::size_t>(i)] = display.lookup(cv->palette->rgb[static_cast<std::size_t>(i)]);
    for (std::size_t i = 0; i < cv->indices.size(); ++i)
        img->pixels[i] = map[cv->indices[i]];
    return img;
}

} // namespace

std::filesystem::path BarbieCamera::photoFile(int slot) {
    // "Photos\" + "photos.omt" + sprintf("%ld", slot), no further extension.
    return paths::userDataDir() / "Photos" / std::format("photos.omt{}", slot);
}

bool BarbieCamera::takePhoto(Godiva& app) {
    // 0x404200: a 0x22D canvas with the node background's palette (node+0x98)
    // holding a copy of the 640x480 screen; then SavePhoto.
    const engine::Node* node = app.currentNode();
    if (!node)
        return false;
    const engine::Image8& screen = app.screen();
    formats::Canvas cv;
    cv.attr = 0x22D;
    cv.width = screen.width;
    cv.height = screen.height;
    cv.depth = 8;
    cv.palette = node->palette();
    cv.indices = screen.pixels;

    // SavePhoto (0x4042d0): open (or create) the database, take the first free
    // id 1..10 (FindFreePhotoId 0x4044a0), write the canvas there.
    const auto path = photoFile(app.saveSlot());
    formats::OmtWriter db;
    if (const auto existing = openDatabase(path)) {
        auto copy = formats::OmtWriter::from(*existing);
        if (!copy) {
            log::warn("BarbieCamera: {}", copy.error());
            return false;
        }
        db = std::move(*copy);
    }
    int id = 0;
    for (int i = 1; i <= kMaxPhotos; ++i) {
        if (!db.contains(formats::chunk::kCanvas, i)) {
            id = i;
            break;
        }
    }
    if (id == 0)
        return false;
    auto body = formats::writeCanvas(cv);
    if (!body) {
        log::warn("BarbieCamera: {}", body.error());
        return false;
    }
    db.put(formats::chunk::kCanvas, id, std::move(*body));
    if (!writeDatabase(path, db))
        return false;
    log::info("BarbieCamera: photo {} saved to {}", id, str::fromPath(path));
    return true;
}

std::shared_ptr<engine::Image8> BarbieCamera::loadThumbnail(Godiva& app, int id, const formats::Palette& display) {
    // 0x404660: LoadPhoto then ScaleCanvasInPlace(canvas, 1) = 220x165.
    auto img = readPhoto(app, id, display);
    return img ? scale(*img, kThumbW, kThumbH, false) : nullptr;
}

std::shared_ptr<engine::Image8> BarbieCamera::loadPhoto(Godiva& app, int id, bool half, const formats::Palette& display) {
    // 0x4048f0: ScaleCanvasInPlace(canvas, 0) = 320x240 when `half`.
    auto img = readPhoto(app, id, display);
    if (img && half)
        return scale(*img, kHalfW, kHalfH, false);
    return img;
}

std::shared_ptr<engine::Image8> BarbieCamera::scale(const engine::Image8& src, int w, int h, bool srcHalf) {
    // StretchBlt from (0, 0, 320, 240) or (0, 0, 640, 480) to (0, 0, w, h)
    // with COLORONCOLOR: each destination pixel takes the source pixel it
    // falls on *(inferred: GDI's exact sampling positions are not known)*.
    const int sw = srcHalf ? kHalfW : 640, sh = srcHalf ? kHalfH : 480;
    auto out = std::make_shared<engine::Image8>(w, h);
    for (int y = 0; y < h; ++y) {
        const int sy = y * sh / h;
        for (int x = 0; x < w; ++x) {
            const int sx = x * sw / w;
            out->at(x, y) = (sx < src.width && sy < src.height) ? src.at(sx, sy) : 0;
        }
    }
    return out;
}

void BarbieCamera::deletePhoto(Godiva& app, int id) {
    // 0x4044e0: removes the chunk ('Canv', id) from the database.
    const auto path = photoFile(app.saveSlot());
    const auto existing = openDatabase(path);
    if (!existing)
        return;
    auto db = formats::OmtWriter::from(*existing);
    if (!db) {
        log::warn("BarbieCamera: {}", db.error());
        return;
    }
    if (db->remove(formats::chunk::kCanvas, id))
        writeDatabase(path, *db);
}

void BarbieCamera::deletePhotoFile(int slot) {
    // 0x404e50.
    std::error_code ec;
    std::filesystem::remove(photoFile(slot), ec);
}

} // namespace brc::game
