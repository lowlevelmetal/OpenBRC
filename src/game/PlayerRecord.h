#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace brc::game {

// The saved player record (app+0x1B0..+0x2FF, `savegame<N>.brc`): 0x150 raw
// little-endian bytes, byte-compatible with the original game's saves.
// Offsets ("rec+X") and meanings are in docs/formats/savegame.md §1.
class PlayerRecord {
public:
    static constexpr std::size_t kSize = 0x150;

    // rec field offsets
    static constexpr std::size_t kName = 0x00, kHorse = 0x10, kSaddle = 0x14, kHorseName = 0x18, kHorseChosen = 0x28,
                                 kRaceHistory = 0x38, kRaceWins = 0x9C, kRaceLosses = 0xA0, kFirstRideDone = 0xA4,
                                 kMissionDone = 0xA8, kCertificate = 0xC8, kBadge = 0xD8, kVoiceVariantA = 0xEC,
                                 kVoiceVariantB = 0xEE, kVoiceVariantC = 0xF0, kQuiltViewed = 0xF2, kPayoffSeen = 0xF4,
                                 kChestOpened = 0xF6, kWellPathOpen = 0xF8, kCottageGateOpen = 0xFA, kHasCoin = 0xFC,
                                 kC1Photos = 0xFE, kC2Photos = 0x104, kRiderRank = 0x10C, kBbVisits = 0x114,
                                 kTeamPhotoViews = 0x118, kClueBookOpens = 0x11C, kCertBookOpens = 0x120,
                                 kHandbookOpens = 0x124, kHorsesPageViews = 0x12C, kTackroomVisits = 0x134,
                                 kRideHintCount = 0x13C, kPagerIntroCount = 0x140, kStableVisits = 0x144,
                                 kFinaleState = 0x148, kHelpHintCount = 0x14C;
    static constexpr std::uint32_t kNoHorse = 4;

    std::array<std::uint8_t, kSize> bytes{};

    std::uint8_t u8(std::size_t off) const { return bytes[off]; }
    std::uint16_t u16(std::size_t off) const { return get<std::uint16_t>(off); }
    std::int16_t s16(std::size_t off) const { return get<std::int16_t>(off); }
    std::uint32_t u32(std::size_t off) const { return get<std::uint32_t>(off); }
    std::int32_t s32(std::size_t off) const { return get<std::int32_t>(off); }
    void setU8(std::size_t off, std::uint8_t v) { bytes[off] = v; }
    void setU16(std::size_t off, std::uint16_t v) { set(off, v); }
    void setS16(std::size_t off, std::int16_t v) { set(off, v); }
    void setU32(std::size_t off, std::uint32_t v) { set(off, v); }
    void setS32(std::size_t off, std::int32_t v) { set(off, v); }

    std::string name() const;
    void setName(std::string_view name); // up to 12 characters, like strncpy(…, 12)
    std::uint32_t horse() const { return u32(kHorse); }
    bool hasHorse() const { return horse() != kNoHorse; }

    // Godiva::Start (0x411c70): the record before any player is chosen.
    void setSessionDefaults();
    // CreateNewSaveGame (0x409f20): a new player.
    static PlayerRecord newPlayer(std::string_view name);

private:
    template <class T>
    T get(std::size_t off) const {
        T v;
        std::memcpy(&v, bytes.data() + off, sizeof v); // little-endian hosts only (all supported targets)
        return v;
    }
    template <class T>
    void set(std::size_t off, T v) {
        std::memcpy(bytes.data() + off, &v, sizeof v);
    }
};

// Saved games under <data dir>/Saved Games: savegame<N>.brc, N = 0..9.
namespace saves {
std::filesystem::path directory();
std::filesystem::path slotPath(int slot);
bool exists(int slot);                                    // SlotExists 0x429440
std::optional<PlayerRecord> load(int slot);               // LoadGame 0x428080
bool save(const PlayerRecord& record, int slot);          // SaveGame 0x401df0
bool remove(int slot);                                    // DeleteSavegame 0x429690 (and the photos)
} // namespace saves

} // namespace brc::game
