#include "Command.h"

#include "core/StringUtil.h"
#include "vfs/DiscManifest.h"

#include <print>

namespace brc::tool {
namespace {

void report(const vfs::GameSource& s) {
    std::println("{}", s.describe());
    std::println("  data files: {}/{}{}", s.provided, vfs::kDiscManifest.size(), s.complete() ? "" : " (incomplete)");
    std::println("  executable: {}", !s.exeFound ? "not present" : s.exeKnown ? "v1.0 (known)" : "unknown build");
}

const Register reg{{
    "sources",
    "[path...]",
    "Probe the given paths (or look for discs, images and installs) and report what they hold.",
    [](Context&, std::span<const std::string> args) {
        std::vector<std::filesystem::path> paths;
        for (const auto& a : args)
            paths.push_back(str::toPath(a));
        if (paths.empty())
            paths = vfs::suggestGameSources();
        if (paths.empty())
            std::println("no Barbie Riding Club disc, image or installation found");
        int rc = 0;
        for (const auto& p : paths) {
            std::string error;
            if (auto s = vfs::probeGameSource(p, &error))
                report(*s);
            else {
                std::println("{}: {}", str::fromPath(p), error);
                rc = 1;
            }
        }
        return rc;
    },
}};

} // namespace
} // namespace brc::tool
