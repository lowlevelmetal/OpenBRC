#pragma once

#include <optional>
#include <string_view>

namespace brc::render {

enum class BackendKind {
    Auto,   // Vulkan, falling back to OpenGL
    Vulkan, // preferred; still falls back to OpenGL when Vulkan is unusable
    OpenGL,
};

enum class Aspect {
    Original, // 4:3 (the monitors of 1998), pillar- or letterboxed
    Stretch,  // fill the window
    Integer,  // largest whole multiple of the source size that fits (4:3 when none does)
};

enum class Filter {
    Sharp,   // nearest-neighbour to the integer scale, bilinear for the remainder
    Nearest, // plain nearest-neighbour (uneven pixel sizes at fractional scales)
    Smooth,  // bilinear
    Crt,     // light scanlines and aperture grille
};

inline constexpr int kAspectCount = 3;
inline constexpr int kFilterCount = 4;

// The user-facing display options, persisted by the application.
struct DisplaySettings {
    BackendKind backend = BackendKind::Auto;
    bool fullscreen = false; // borderless at the desktop resolution
    int windowWidth = 1280;  // windowed size in window units (points on HiDPI displays)
    int windowHeight = 960;
    Aspect aspect = Aspect::Original;
    Filter filter = Filter::Sharp;
    bool vsync = true;

    friend bool operator==(const DisplaySettings&, const DisplaySettings&) = default;
};

// Stable lowercase names for configuration files ("auto", "original", "crt", ...).
std::string_view toString(BackendKind kind);
std::string_view toString(Aspect aspect);
std::string_view toString(Filter filter);
// Case-insensitive; also accept "vk" / "gl" and "4:3".
std::optional<BackendKind> parseBackendKind(std::string_view text);
std::optional<Aspect> parseAspect(std::string_view text);
std::optional<Filter> parseFilter(std::string_view text);

// Names for on-screen messages ("4:3", "Integer scale", "CRT", ...).
std::string_view displayName(Aspect aspect);
std::string_view displayName(Filter filter);

Aspect nextAspect(Aspect aspect);
Filter nextFilter(Filter filter);

} // namespace brc::render
