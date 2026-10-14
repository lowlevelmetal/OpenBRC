#include "TestData.h"

#include "core/Log.h"
#include "core/StringUtil.h"
#include "vfs/GameSource.h"

#include <cstdlib>
#include <memory>

namespace brc::test {

const vfs::Vfs* gameData() {
    static const std::unique_ptr<vfs::GameData> instance = []() -> std::unique_ptr<vfs::GameData> {
        const char* env = std::getenv("OPENBRC_GAME_DATA");
        if (!env || !*env)
            return nullptr;
#ifdef _WIN32
        constexpr char kSep = ';';
#else
        constexpr char kSep = ':';
#endif
        std::vector<vfs::GameSource> sources;
        for (auto part : str::split(env, kSep)) {
            if (part.empty())
                continue;
            std::string error;
            auto source = vfs::probeGameSource(str::toPath(part), &error);
            if (!source) {
                log::error("OPENBRC_GAME_DATA: {}", error);
                return nullptr;
            }
            sources.push_back(std::move(*source));
        }
        auto data = std::make_unique<vfs::GameData>(vfs::assembleGameData(std::move(sources)));
        if (!data->usable()) {
            log::error("OPENBRC_GAME_DATA: {} required files missing", data->missing.size());
            return nullptr;
        }
        return data;
    }();
    return instance ? &instance->vfs : nullptr;
}

} // namespace brc::test
