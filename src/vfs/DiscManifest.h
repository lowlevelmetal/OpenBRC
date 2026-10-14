#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace brc::vfs {

struct ManifestEntry {
    std::string_view path; // normalized virtual path, e.g. "resources/common.omt"
    std::uint32_t size;
};

// Every data file the game reads, as found on the v1.0 disc.
extern const std::array<ManifestEntry, 336> kDiscManifest;

// Identity of the supported release.
inline constexpr std::string_view kDiscVolumeId = "BRCV1_0";
inline constexpr std::uint64_t kDiscImageSize = 652183552;
inline constexpr std::string_view kDiscImageSha256 =
    "d83c92d228f0861a2d6d74e2b21759aea479b4d2d79c9a2b266341998db23c5a";
inline constexpr std::string_view kGameExeName = "Barbie Riding Club.exe";
inline constexpr std::string_view kGameExeSha256 =
    "74151dbab5d58ae808a4a53670c0b1f94b816d2fd8e0243e87a93c4f32d56c75";

} // namespace brc::vfs
