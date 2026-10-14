#pragma once

#include "engine/Image8.h"
#include "formats/Canvas.h"

#include <filesystem>
#include <memory>

namespace brc::game {

class Godiva;

// BarbieCamera (BarbieCamera.cpp 0x403f20–0x404f6f, the object at app+0x184
// has no members): the photo store of the toolbar camera. Each player's
// photos live in Photos/photos.omt<slot>, an OMT database of up to 10 'Canv'
// chunks with ids 1..10 (docs/formats/savegame.md §3). Images come back
// converted to the `display` palette, as the original's canvas reader does.
class BarbieCamera {
public:
    static constexpr int kMaxPhotos = 10;
    // Thumbnail and half-size view (ScaleCanvasInPlace 0x404cf0: 0x7fe3c4..d0).
    static constexpr int kThumbW = 220, kThumbH = 165, kHalfW = 320, kHalfH = 240;

    // <user data>/Photos/photos.omt<slot> (s_photosDir/s_photosFile statics).
    static std::filesystem::path photoFile(int slot);
    // 0x404200: the 640x480 screen with the current node's background palette,
    // saved by SavePhoto (0x4042d0) under the first free id. False when all ten
    // ids are taken (the toolbar then plays no shutter sound).
    static bool takePhoto(Godiva& app);
    // 0x404660: photo `id` scaled to 220x165, or null.
    static std::shared_ptr<engine::Image8> loadThumbnail(Godiva& app, int id, const formats::Palette& display);
    // 0x4048f0: photo `id` at 640x480, or scaled to 320x240 when `half`.
    static std::shared_ptr<engine::Image8> loadPhoto(Godiva& app, int id, bool half, const formats::Palette& display);
    // 0x404b90: a new w x h image from the source's top-left 320x240 (half)
    // or 640x480, StretchBlt mode COLORONCOLOR (nearest pixel).
    static std::shared_ptr<engine::Image8> scale(const engine::Image8& src, int w, int h, bool srcHalf);
    // 0x4044e0: removes chunk `id` of the current player's database.
    static void deletePhoto(Godiva& app, int id);
    // 0x404e50: deletes a player's whole photo database.
    static void deletePhotoFile(int slot);
};

} // namespace brc::game
