#include "engine/Resources.h"

#include "core/Log.h"
#include "core/StringUtil.h"

#include <algorithm>

namespace brc::engine {

std::string resourcePath(std::string_view fileName) { return "resources/" + str::lower(fileName); }

Resources::OpenFile* Resources::findOpen(std::string_view fileName) {
    const std::string key = str::lower(fileName);
    for (auto& f : m_files)
        if (f.name == key)
            return &f;
    return nullptr;
}

const Resources::OpenFile* Resources::findOpen(std::string_view fileName) const {
    return const_cast<Resources*>(this)->findOpen(fileName);
}

bool Resources::open(std::string_view fileName, bool prepend) {
    if (OpenFile* f = findOpen(fileName)) {
        ++f->refs;
        return true;
    }
    auto file = m_data.open(resourcePath(fileName));
    if (!file)
        return false;
    auto omt = formats::OmtFile::open(file, std::string(fileName));
    if (!omt) {
        log::warn("resources: {}", omt.error());
        return false;
    }
    OpenFile entry{str::lower(fileName), 1, std::make_unique<formats::OmtFile>(std::move(*omt))};
    if (prepend)
        m_files.push_front(std::move(entry));
    else
        m_files.push_back(std::move(entry));
    log::debug("resources: opened {}", fileName);
    return true;
}

void Resources::close(std::string_view fileName) {
    if (OpenFile* f = findOpen(fileName))
        --f->refs;
}

void Resources::purge() {
    for (auto it = m_files.begin(); it != m_files.end();) {
        if (it->refs < 1) {
            const formats::OmtFile* omt = it->omt.get();
            std::erase_if(m_images, [&](const auto& kv) {
                for (const auto& c : omt->chunks())
                    if (&c == kv.first.first)
                        return true;
                return false;
            });
            log::debug("resources: closed {}", it->name);
            it = m_files.erase(it);
        } else {
            ++it;
        }
    }
}

void Resources::closeAll() {
    m_images.clear();
    m_files.clear();
}

bool Resources::isOpen(std::string_view fileName) const { return findOpen(fileName) != nullptr; }

std::vector<std::string> Resources::openFiles() const {
    std::vector<std::string> out;
    for (const auto& f : m_files)
        out.push_back(f.name);
    return out;
}

Resources::Found Resources::find(formats::FourCC type, std::string_view name) const {
    for (const auto& f : m_files)
        if (const auto* c = f.omt->find(type, name))
            return {f.omt.get(), c};
    return {};
}

std::vector<std::byte> Resources::read(const Found& f) const { return f.file->read(*f.chunk); }

std::shared_ptr<RandomAccessFile> Resources::openChunk(const Found& f) const { return f.file->openChunk(*f.chunk); }

std::shared_ptr<const Image8> Resources::image(std::string_view name, const formats::Palette& display) {
    // BLIT first (8-bit screen), never remapped.
    if (Found b = find(formats::chunk::kBlit, name)) {
        auto key = std::pair{b.chunk, std::array<std::uint32_t, 256>{}};
        if (auto it = m_images.find(key); it != m_images.end())
            return it->second;
        auto blit = formats::parseBlit(read(b));
        if (!blit) {
            log::warn("resources: {} '{}': {}", b.file->label(), name, blit.error());
        } else {
            auto img = std::make_shared<Image8>();
            img->width = blit->width;
            img->height = blit->height;
            img->pixels = std::move(blit->pixels);
            img->opaque = std::move(blit->opaque);
            m_images[key] = img;
            return img;
        }
    }
    Found c = find(formats::chunk::kCanvas, name);
    if (!c)
        return nullptr;
    auto key = std::pair{c.chunk, display.rgb};
    if (auto it = m_images.find(key); it != m_images.end())
        return it->second;
    auto cv = formats::parseCanvas(read(c));
    if (!cv) {
        log::warn("resources: {} '{}': {}", c.file->label(), name, cv.error());
        return nullptr;
    }
    auto img = std::make_shared<Image8>(cv->width, cv->height);
    if (cv->depth <= 8) {
        // Remap to the display palette when the canvas's own differs (0x471e60);
        // scene sprites share their scene's palette, so this is usually a copy.
        std::array<std::uint8_t, 256> map{};
        for (int i = 0; i < 256; ++i)
            map[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(i);
        if (cv->depth == 1) {
            map[0] = display.nearest(0xFFFFFF); // white
            map[1] = display.nearest(0x000000); // black
        } else if (cv->palette && !(cv->palette->rgb == display.rgb)) {
            for (int i = 0; i < 256; ++i)
                map[static_cast<std::size_t>(i)] = display.nearest(cv->palette->rgb[static_cast<std::size_t>(i)]);
        }
        for (std::size_t i = 0; i < cv->indices.size(); ++i)
            img->pixels[i] = map[cv->indices[i]];
        if (cv->hasKey) {
            img->opaque.resize(img->pixels.size());
            for (std::size_t i = 0; i < cv->indices.size(); ++i)
                img->opaque[i] = cv->indices[i] != cv->key;
        }
    } else {
        for (std::size_t i = 0; i < cv->rgb.size(); ++i)
            img->pixels[i] = display.nearest(cv->rgb[i]);
        if (cv->hasKey) {
            img->opaque.resize(img->pixels.size());
            for (std::size_t i = 0; i < cv->rgb.size(); ++i)
                img->opaque[i] = cv->rgb[i] != cv->key;
        }
    }
    m_images[key] = img;
    return img;
}

std::optional<formats::Palette> Resources::canvasPalette(std::string_view name) const {
    Found c = find(formats::chunk::kCanvas, name);
    if (!c)
        return std::nullopt;
    auto p = formats::parseCanvasPalette(read(c));
    if (!p || !*p)
        return std::nullopt;
    return std::move(**p);
}

std::optional<formats::Canvas> Resources::canvas(std::string_view name) const {
    Found c = find(formats::chunk::kCanvas, name);
    if (!c)
        return std::nullopt;
    auto cv = formats::parseCanvas(read(c));
    if (!cv)
        return std::nullopt;
    return std::move(*cv);
}

std::optional<formats::AnimCache> Resources::openCache(std::string_view fileName) const {
    auto file = m_data.open(resourcePath(fileName));
    if (!file)
        return std::nullopt;
    auto cache = formats::openAnimCache(file, std::string(fileName));
    if (!cache) {
        log::warn("resources: {}", cache.error());
        return std::nullopt;
    }
    return std::move(*cache);
}

std::shared_ptr<RandomAccessFile> Resources::openFile(std::string_view path) const { return m_data.open(path); }

} // namespace brc::engine
