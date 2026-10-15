#include "game/PlayerRecord.h"

#include "core/File.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "core/StringUtil.h"

#include <format>

namespace brc::game {

std::string PlayerRecord::name() const {
    const char* p = reinterpret_cast<const char*>(bytes.data() + kName);
    return std::string(p, strnlen(p, 13));
}

void PlayerRecord::setName(std::string_view name) {
    std::memset(bytes.data() + kName, 0, 12);
    std::memcpy(bytes.data() + kName, name.data(), std::min<std::size_t>(name.size(), 12));
    bytes[0x0C] = 0;
}

namespace {

void raceHistoryDefaults(PlayerRecord& r) {
    for (int i = 0; i < 5; ++i) {
        const std::size_t e = PlayerRecord::kRaceHistory + 0x14 * static_cast<std::size_t>(i);
        r.setU32(e + 0x0, 0);
        r.setU16(e + 0x4, 0);
        r.setU32(e + 0x8, 0);
        r.setU32(e + 0xC, 0);
        r.setU32(e + 0x10, 1);
    }
}

} // namespace

void PlayerRecord::setSessionDefaults() {
    // Godiva::Start (0x411c70) sets only these fields; the rest (the horse
    // names included) keep the zeroes of the freshly created application.
    bytes.fill(0);
    setName("Barbie");
    setU32(kHorse, kNoHorse);
    setU32(kSaddle, 2);
    raceHistoryDefaults(*this);
}

PlayerRecord PlayerRecord::newPlayer(std::string_view name) {
    PlayerRecord r;
    r.setName(name);
    r.setU32(kHorse, kNoHorse);
    r.setU32(kSaddle, 2);
    for (int i = 0; i < 4; ++i) {
        r.setS32(kHorseName + 4 * static_cast<std::size_t>(i), -1);
        r.setU32(kHorseChosen + 4 * static_cast<std::size_t>(i), 0);
    }
    raceHistoryDefaults(r);
    return r;
}

namespace saves {

std::filesystem::path directory() {
    auto dir = paths::userDataDir() / "Saved Games";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

std::filesystem::path slotPath(int slot) { return directory() / std::format("savegame{}.brc", slot); }

bool exists(int slot) {
    std::error_code ec;
    return std::filesystem::is_regular_file(slotPath(slot), ec);
}

std::optional<PlayerRecord> load(int slot) {
    auto data = file::readBinary(slotPath(slot));
    if (!data || data->size() < PlayerRecord::kSize)
        return std::nullopt;
    PlayerRecord r;
    std::memcpy(r.bytes.data(), data->data(), PlayerRecord::kSize);
    return r;
}

bool save(const PlayerRecord& record, int slot) {
    const bool ok = file::writeAtomic(slotPath(slot), std::as_bytes(std::span(record.bytes)));
    if (!ok)
        log::warn("cannot write {}", str::fromPath(slotPath(slot)));
    return ok;
}

bool remove(int slot) {
    std::error_code ec;
    const bool ok = std::filesystem::remove(slotPath(slot), ec);
    std::filesystem::remove(paths::userDataDir() / "Photos" / std::format("photos.omt{}", slot), ec);
    return ok;
}

} // namespace saves

} // namespace brc::game
