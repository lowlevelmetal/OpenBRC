#include "engine/Anim.h"

#include "core/Log.h"
#include "core/StringUtil.h"
#include "engine/Engine.h"
#include "engine/Node.h"
#include "engine/Scripts.h"
#include "engine/SoundManager.h"

#include <format>

namespace brc::engine {
namespace {

using namespace formats::anim_flags;

constexpr std::uint8_t kKey = 0xFE; // Smacker sprite colour key (magenta in the game palette)

} // namespace

// --- loading (0x447cc0, v2 loader 0x4485b0) -------------------------------------

std::unique_ptr<Anim> Anim::load(Engine& engine, std::string_view name, const CacheSource* cache) {
    Resources& res = engine.resources();
    std::vector<std::byte> descBytes;
    int chunkId = 0;
    if (cache) {
        const auto* c = cache->container ? cache->container->find(formats::chunk::kAnim, name) : nullptr;
        if (c) {
            descBytes = cache->container->read(*c);
            chunkId = c->id;
        }
    } else if (auto f = res.find(formats::chunk::kAnim, name)) {
        descBytes = res.read(f);
        chunkId = f.chunk->id;
    }
    if (descBytes.empty()) {
        log::warn("Error: can't find animation {}.", name);
        return nullptr;
    }
    auto desc = formats::parseHcAnim(descBytes);
    if (!desc) {
        log::warn("anim {}: {}", name, desc.error());
        return nullptr;
    }

    std::unique_ptr<Anim> a(new Anim(engine));
    a->m_name = name;
    a->setElementId(chunkId);
    a->m_desc = std::make_shared<formats::HcAnimDesc>(std::move(*desc));
    a->m_offsets = a->m_desc->sourceOffsets;
    const std::size_t nsrc = a->m_offsets.size();
    a->m_sources.resize(nsrc);

    // Frame sources: the cache record, else SMK2 (full frames), else SMAK+SMKI
    // (cropped), else still images "<name>_<i>".
    std::shared_ptr<const RandomAccessFile> smkData;
    std::vector<formats::SmkiEntry> smki;
    if (!(a->m_desc->flags & kNoSources)) {
        if (cache && cache->data) {
            smkData = cache->data;
            a->m_cropped = cache->cropped;
            if (cache->cropped) {
                // The SMKI records follow the Smacker data inside the record.
                const std::uint64_t n = 6 * nsrc;
                std::vector<std::byte> tail(static_cast<std::size_t>(n));
                if (cache->data->size() >= n && cache->data->readExact(cache->data->size() - n, tail))
                    smki = formats::parseSmki(tail);
            }
        } else if (auto f = res.find(formats::chunk::kSmacker2, name)) {
            smkData = res.openChunk(f);
        } else if (auto f2 = res.find(formats::chunk::kSmacker, name)) {
            smkData = res.openChunk(f2);
            a->m_cropped = true;
            if (auto i = res.find(formats::chunk::kSmackerInfo, name))
                smki = formats::parseSmki(res.read(i));
        }
        if (smkData) {
            auto smk = formats::Smacker::open(smkData);
            if (!smk) {
                log::warn("anim {}: {}", name, smk.error());
                return nullptr;
            }
            a->m_smk = std::make_unique<formats::Smacker>(std::move(*smk));
            a->m_canvas = Image8(static_cast<int>(a->m_smk->width()), static_cast<int>(a->m_smk->height()), kKey);
            a->m_canvas.key = kKey;
            a->m_smk->setTarget(a->m_canvas.pixels, static_cast<std::size_t>(a->m_canvas.width));
            for (std::size_t i = 0; i < nsrc && i < smki.size(); ++i) {
                a->m_sources[i].contentW = smki[i].width;
                a->m_sources[i].contentH = smki[i].height;
            }
        } else {
            for (std::size_t i = 0; i < nsrc; ++i) {
                a->m_sources[i].still = res.image(std::format("{}_{}", name, i + 1), engine.gamePalette());
                if (!a->m_sources[i].still)
                    log::debug("anim {}: no image {}_{}", name, name, i + 1);
            }
        }
    }
    a->m_frame = -1;
    return a;
}

Anim::~Anim() = default;

void Anim::addFrameFlags(int seq, int frame, std::uint32_t flags) {
    if (seq < 0 || seq >= sequenceCount())
        return;
    auto& frames = m_desc->sequences[static_cast<std::size_t>(seq)].frames;
    if (frame < 0 || static_cast<std::size_t>(frame) >= frames.size())
        return;
    frames[static_cast<std::size_t>(frame)].flags |= flags;
}

int Anim::frameCount(int seq) const {
    if (seq < 0 || seq >= sequenceCount())
        return 0;
    return static_cast<int>(m_desc->sequences[static_cast<std::size_t>(seq)].frames.size());
}

const formats::HcAnimFrame* Anim::current() const {
    if (m_frame < 0 || m_seq < 0 || m_seq >= sequenceCount())
        return nullptr;
    const auto& frames = m_desc->sequences[static_cast<std::size_t>(m_seq)].frames;
    if (static_cast<std::size_t>(m_frame) >= frames.size())
        return nullptr;
    return &frames[static_cast<std::size_t>(m_frame)];
}

// --- Element ---------------------------------------------------------------------

const Image8* Anim::image() const {
    const auto* f = current();
    if (!f)
        return nullptr;
    if (m_smk)
        return &m_canvas;
    const std::size_t i = static_cast<std::size_t>(f->source) - 1;
    return i < m_sources.size() ? m_sources[i].still.get() : nullptr;
}

float Anim::imageOffsetX() const {
    const auto* f = current();
    const std::size_t i = f ? static_cast<std::size_t>(f->source) - 1 : 0;
    return f && i < m_offsets.size() ? static_cast<float>(m_offsets[i].x) : 0.0f;
}

float Anim::imageOffsetY() const {
    const auto* f = current();
    const std::size_t i = f ? static_cast<std::size_t>(f->source) - 1 : 0;
    return f && i < m_offsets.size() ? static_cast<float>(m_offsets[i].y) : 0.0f;
}

void Anim::recenter() {
    const Image8* img = image();
    if (!img)
        return;
    for (auto& o : m_offsets) {
        o.x = static_cast<std::int16_t>(-img->width / 2);
        o.y = static_cast<std::int16_t>(-img->height / 2);
    }
    setPosition(0, 0, m_z);
}

std::pair<float, float> Anim::frameOffset(int seq, int index) const {
    if (index < 0 || index >= frameCount(seq))
        return {0.0f, 0.0f};
    const auto& f = m_desc->sequences[static_cast<std::size_t>(seq)].frames[static_cast<std::size_t>(index)];
    const std::size_t i = static_cast<std::size_t>(f.source) - 1;
    if (i >= m_offsets.size())
        return {0.0f, 0.0f};
    return {static_cast<float>(m_offsets[i].x), static_cast<float>(m_offsets[i].y)};
}

void Anim::setFrameOffset(int seq, int index, float x, float y) {
    if (index < 0 || index >= frameCount(seq))
        return;
    const auto& f = m_desc->sequences[static_cast<std::size_t>(seq)].frames[static_cast<std::size_t>(index)];
    const std::size_t i = static_cast<std::size_t>(f.source) - 1;
    if (i >= m_offsets.size())
        return;
    m_offsets[i].x = static_cast<std::int16_t>(x);
    m_offsets[i].y = static_cast<std::int16_t>(y);
    markChanged();
}

void Anim::pause(bool on) {
    // 0x447b50: counts motion and playback; HC clamps both at 0. Unpausing
    // playback from 1 clears "started", so the frame shows for a whole duration.
    Element::pause(on);
    if (on) {
        ++m_pause;
    } else if (m_pause > 0) {
        if (--m_pause == 0)
            m_started = false;
    }
}

void Anim::update(std::uint32_t elapsedMs) {
    Element::update(elapsedMs);
    run(elapsedMs);
}

// --- playback ---------------------------------------------------------------------

void Anim::setSequence(int seq, bool restart) {
    if (seq < 0 || seq >= sequenceCount())
        return;
    m_stopped = false;
    if (hideCount() != 0) {
        setHideCount(1);
        hide(false); // selecting a sequence always shows the anim
    }
    // Base (0x479f50): switch sequence, keeping the frame index unless restarting.
    if (seq != m_seq || m_frame < 0) {
        const int n = frameCount(seq);
        int index = 0;
        if (!restart && m_frame >= 0)
            index = std::min(m_frame, n - 1);
        m_seq = seq;
        m_frame = n > 0 ? index : -1;
        markChanged();
        m_started = false;
    }
    if (!m_owner)
        return;
    if (restart) {
        m_frame = frameCount(seq) > 0 ? 0 : -1;
        m_started = false;
        markChanged();
    }
    m_owner->sequenceStarted(*this, seq);
    const auto& info = m_desc->sequences[static_cast<std::size_t>(seq)];
    if (!(m_desc->flags & kKeepDepth))
        setPosition(m_x, m_y, static_cast<float>(info.z));
    m_loop = info.loops();
    if (current() && processFrame())
        m_owner->sequenceEnded(*this, m_seq);
}

void Anim::setSequenceFrame(int seq, int index) {
    setSequence(seq, true);
    if (seq != m_seq || index < 0 || index >= frameCount(seq))
        return;
    m_frame = index;
    markChanged();
    processFrame();
}

void Anim::run(std::uint32_t elapsedMs) {
    // Run (0x47a290).
    if (m_pause != 0)
        return;
    const int oldSeq = m_seq, oldFrame = m_frame;
    if (m_frame < 0) {
        if (frameCount(m_seq) == 0)
            return;
        m_frame = 0;
        m_started = false;
    }
    if (m_desc->flags & kTimeBased) {
        if (!m_started) {
            m_started = true; // no advance on the first update
            m_remaining = static_cast<float>(current()->durationMs);
        } else {
            float e = static_cast<float>(elapsedMs);
            if (current()->durationMs != 0) { // a 0 ms frame never advances
                bool done = false;
                while (e >= m_remaining) {
                    e -= m_remaining;
                    if (advance()) {
                        m_remaining = current() ? static_cast<float>(current()->durationMs) : 0.0f;
                        done = true;
                        break;
                    }
                    m_remaining = static_cast<float>(current()->durationMs);
                    if (m_remaining == 0) {
                        done = true;
                        break;
                    }
                }
                if (!done)
                    m_remaining -= e;
            }
        }
    } else {
        if (!m_started) {
            m_started = true;
            m_ticks = 0;
        } else if (++m_ticks >= 1) {
            m_ticks = 0;
            advance();
        }
    }
    if (m_seq != oldSeq || m_frame != oldFrame)
        markChanged();
}

bool Anim::nextFrame() {
    // 0x47a3e0, forward only (HC never plays backwards or ping-pong).
    const int n = frameCount(m_seq);
    if (n <= 1)
        return true;
    if (m_frame + 1 < n) {
        ++m_frame;
        return false;
    }
    if (m_loop) {
        m_frame = 0;
        return false;
    }
    return true;
}

bool Anim::advance() {
    // 0x449660.
    if (m_stopped)
        return true;
    const auto* f = current();
    if (f && (f->flags & kFrameGoto)) {
        setSequence(f->gotoSequence, true);
        return true;
    }
    const bool ended = nextFrame();
    const bool stop = !ended && processFrame();
    if ((ended || stop) && m_owner)
        m_owner->sequenceEnded(*this, m_seq);
    if (ended && !m_stopped) {
        hide(true); // a sequence that runs out hides the anim
        m_stopped = true;
    }
    return ended || stop;
}

bool Anim::processFrame() {
    // 0x4497b0, each time a frame becomes current.
    const formats::HcAnimFrame* f = current();
    if (!f)
        return false;
    decodeSmackerFrame();
    bool stop = false;
    if (f->flags & kFrameStop) {
        m_stopped = true;
        stop = true;
    }
    // Copy what the actions need: callbacks may change this anim.
    const auto flags = f->flags;
    const formats::HcAnimFrame frame = *f;
    if ((flags & kFrameEvent) && m_owner)
        m_owner->frameEvent(*this, m_seq, m_frame);
    if (flags & kFrameSound)
        m_engine.sounds().playSound(frame.sound, -1, false, false, 1, false, -1, -1, 0, false);
    if (flags & kFrameSetSeq)
        if (Anim* other = m_engine.anims().getOrCreate(frame.setSeqAnim))
            other->setSequence(frame.setSeqIndex, true);
    if (flags & kFramePauseAnim)
        if (Anim* other = m_engine.anims().find(frame.pauseAnim)) {
            other->pause(true);
            other->hide(true);
        }
    Node* node = m_owner ? m_owner : m_engine.currentNode();
    if ((flags & kFrameStartScript) && node)
        m_engine.scripts().runScript(*node, frame.startScript);
    if ((flags & kFrameStopScript) && node)
        m_engine.scripts().stopScript(*node, frame.stopScript);
    return stop;
}

void Anim::decodeSmackerFrame() {
    // 0x449920: random access to the source frame (1-based, SmackGoto), then
    // for cropped frames everything outside the content box becomes the key.
    const auto* f = current();
    if (!m_smk || !f)
        return;
    m_smk->gotoFrame(static_cast<std::uint32_t>(f->source) - 1);
    if (auto r = m_smk->decodeFrame(); !r) {
        log::warn("anim {}: {}", m_name, r.error());
        return;
    }
    if (m_cropped) {
        const std::size_t i = static_cast<std::size_t>(f->source) - 1;
        const int cw = i < m_sources.size() ? m_sources[i].contentW : m_canvas.width;
        const int ch = i < m_sources.size() ? m_sources[i].contentH : m_canvas.height;
        for (int y = 0; y < m_canvas.height; ++y)
            for (int x = (y < ch ? cw : 0); x < m_canvas.width; ++x)
                m_canvas.at(x, y) = kKey;
    }
    m_decodedSource = f->source;
    markChanged();
}

// --- registry ----------------------------------------------------------------------

AnimRegistry::~AnimRegistry() { deleteAll(); }

Anim* AnimRegistry::find(std::string_view name) const {
    const auto it = m_anims.find(str::lower(name));
    return it == m_anims.end() ? nullptr : it->second.get();
}

namespace {
void attach(Anim& a, Node* node) {
    a.setOwner(node);
    a.link(node);
    a.setPosition(0, 0, a.z());
    a.setSequence(0, true);
}
} // namespace

Anim* AnimRegistry::getOrCreate(std::string_view name, std::string_view newName, const Anim::CacheSource* cache) {
    if (name.empty())
        return nullptr;
    Node* node = m_engine.currentNode();
    if (newName.empty()) {
        if (Anim* existing = find(name)) {
            if (!existing->node() && node)
                attach(*existing, node);
            return existing;
        }
    }
    auto anim = Anim::load(m_engine, name, cache);
    if (!anim)
        return nullptr;
    if (!cache && node)
        attach(*anim, node);
    if (!newName.empty())
        anim->rename(newName);
    Anim* raw = anim.get();
    auto& slot = m_anims[str::lower(anim->name())];
    if (slot)
        m_replaced.push_back(std::move(slot)); // the original leaks the overwritten anim
    slot = std::move(anim);
    return raw;
}

void AnimRegistry::remove(std::string_view name) {
    const auto it = m_anims.find(str::lower(name));
    if (it == m_anims.end())
        return;
    it->second->hide(true);
    m_deleted.push_back(std::move(it->second));
    m_anims.erase(it);
}

void AnimRegistry::remove(Anim* anim) {
    for (auto it = m_anims.begin(); it != m_anims.end(); ++it) {
        if (it->second.get() == anim) {
            anim->hide(true);
            m_deleted.push_back(std::move(it->second));
            m_anims.erase(it);
            return;
        }
    }
}

void AnimRegistry::flushDeleted() { m_deleted.clear(); }

void AnimRegistry::deleteAll() {
    m_deleted.clear();
    m_anims.clear();
    m_replaced.clear();
}

void AnimRegistry::deleteLinkedTo(const Node* node) {
    for (auto it = m_anims.begin(); it != m_anims.end();) {
        if (it->second->node() == node)
            it = m_anims.erase(it);
        else
            ++it;
    }
    std::erase_if(m_replaced, [&](const auto& a) { return a->node() == node; });
    std::erase_if(m_deleted, [&](const auto& a) { return a->node() == node; });
}

int AnimRegistry::preloadCache(std::string_view fileName) {
    auto cache = m_engine.resources().openCache(fileName);
    if (!cache)
        return 0;
    auto file = m_engine.resources().openFile(resourcePath(fileName));
    int created = 0;
    for (const auto& rec : cache->records) {
        if (find(rec.name))
            continue;
        Anim::CacheSource src{&cache->container, std::make_shared<SubFile>(file, rec.offset, rec.size), rec.cropped};
        auto anim = Anim::load(m_engine, rec.name, &src);
        if (!anim)
            break; // the loop stops when creating an anim fails
        m_anims[str::lower(rec.name)] = std::move(anim);
        ++created;
    }
    return created;
}

} // namespace brc::engine
