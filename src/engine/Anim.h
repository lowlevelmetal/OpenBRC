#pragma once

#include "engine/Element.h"
#include "formats/HcAnim.h"
#include "formats/Smacker.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace brc::engine {

class Engine;

// HC2DAnim (HC2DAnim.cpp, 0x447a00–0x449f20): a sprite with sequences of
// frames from an HCAn description, its pixels from a Smacker file or still
// images. See docs/engine/animation.md and docs/formats/hcanim.md.
class Anim final : public Element {
public:
    // Where the frames come from when the anim is created from a cache record.
    struct CacheSource {
        const formats::OmtFile* container = nullptr; // the cache's own HCAn descriptions
        std::shared_ptr<const RandomAccessFile> data; // the record's Smacker (+ SMKI) bytes
        bool cropped = false;
    };

    // Loads the description named `name` (0x447cc0). Returns nullptr (after
    // logging "Error: can't find animation %s.") when it does not exist.
    static std::unique_ptr<Anim> load(Engine& engine, std::string_view name, const CacheSource* cache = nullptr);
    ~Anim() override;

    const std::string& name() const { return m_name; }
    void rename(std::string_view name) { m_name = name; }
    const formats::HcAnimDesc& desc() const { return *m_desc; }

    // --- playback (sequence player at +0xc4) ------------------------------------
    // 0x449540: shows the anim, selects `seq`; restart = from its first frame.
    void setSequence(int seq, bool restart = true);
    // 0x449780: SetSequence(seq, 1), then frame #index of it, processed.
    void setSequenceFrame(int seq, int index);
    int sequence() const { return m_seq; }               // +0xd8
    int sequenceCount() const { return static_cast<int>(m_desc->sequences.size()); }
    int frameIndex() const { return m_frame; }            // 0x47a230
    int frameCount(int seq) const;
    bool stopped() const { return m_stopped; }            // +0x148
    bool playing() const { return !m_stopped && m_pause == 0; }
    void pause(bool on) override;                          // 0x447b50
    int playbackPauseCount() const { return m_pause; }
    // 0x449e40: every frame offset becomes (-w/2, -h/2) of the current image
    // and the position (0, 0).
    void recenter();

    // --- ownership --------------------------------------------------------------
    Node* owner() const { return m_owner; } // +0x114: receives the callbacks
    void setOwner(Node* node) { m_owner = node; }

    // --- Element -----------------------------------------------------------------
    const Image8* image() const override;
    float imageOffsetX() const override;
    float imageOffsetY() const override;
    void update(std::uint32_t elapsedMs) override; // 0x4718e0: motion, then Run

private:
    struct Source {
        std::shared_ptr<const Image8> still; // still-image frames
        int contentW = 0, contentH = 0;      // SMKI size of cropped Smacker frames
    };

    explicit Anim(Engine& engine) : m_engine(engine) {}
    const formats::HcAnimFrame* current() const;
    void run(std::uint32_t elapsedMs);  // 0x47a290
    bool advance();                     // 0x449660
    bool nextFrame();                   // 0x47a3e0: returns "ended"
    bool processFrame();                // 0x4497b0: returns "stop"
    void decodeSmackerFrame();          // 0x449920
    void notifyEnded();

    Engine& m_engine;
    std::string m_name;
    std::shared_ptr<const formats::HcAnimDesc> m_desc;
    std::vector<formats::HcAnimOffset> m_offsets; // per source; changed by recenter()
    std::vector<Source> m_sources;
    std::unique_ptr<formats::Smacker> m_smk;
    Image8 m_canvas;               // Smacker decode target, key 0xFE
    bool m_cropped = false;        // +0x124
    int m_decodedSource = 0;       // source index shown in m_canvas

    Node* m_owner = nullptr;
    int m_seq = 0;
    int m_frame = 0;               // index in the current sequence, -1 = none
    bool m_started = false;        // +0xea
    bool m_loop = false;           // +0xe6
    bool m_stopped = false;        // +0x148
    int m_pause = 0;               // +0xe0
    int m_ticks = 0;               // +0xdc
    float m_remaining = 0;         // +0xf0
};

// The global name -> anim map (0x80a410) with its deletion queue (0x80a430).
// Anims are not owned by nodes; unlinked anims survive node switches.
class AnimRegistry {
public:
    explicit AnimRegistry(Engine& engine) : m_engine(engine) {}
    ~AnimRegistry();

    Anim* find(std::string_view name) const; // 0x455020
    // 0x454ce0 GetOrCreateAnim(name, newName, cache).
    Anim* getOrCreate(std::string_view name, std::string_view newName = {}, const Anim::CacheSource* cache = nullptr);
    // 0x4550b0: hide, forget, delete at the next node tick.
    void remove(std::string_view name);
    void remove(Anim* anim);
    void flushDeleted();     // 0x4552d0
    void deleteAll();        // 0x4551b0
    // A node is being destroyed (0x454880): its linked anims are deleted.
    void deleteLinkedTo(const Node* node);
    // Preloads every anim of a ".omt.cache" file (0x44d620, cache branch).
    int preloadCache(std::string_view fileName);

    std::size_t size() const { return m_anims.size(); }

private:
    Engine& m_engine;
    std::map<std::string, std::unique_ptr<Anim>> m_anims; // key: lower-case name
    std::vector<std::unique_ptr<Anim>> m_deleted;
    std::vector<std::unique_ptr<Anim>> m_replaced; // overwritten map entries (leaked in the original)
};

} // namespace brc::engine
