#include "render/DisplaySettings.h"

#include "core/StringUtil.h"

namespace brc::render {

std::string_view toString(BackendKind kind) {
    switch (kind) {
    case BackendKind::Auto:
        return "auto";
    case BackendKind::Vulkan:
        return "vulkan";
    case BackendKind::OpenGL:
        return "opengl";
    }
    return "auto";
}

std::string_view toString(Aspect aspect) {
    switch (aspect) {
    case Aspect::Original:
        return "original";
    case Aspect::Stretch:
        return "stretch";
    case Aspect::Integer:
        return "integer";
    }
    return "original";
}

std::string_view toString(Filter filter) {
    switch (filter) {
    case Filter::Sharp:
        return "sharp";
    case Filter::Nearest:
        return "nearest";
    case Filter::Smooth:
        return "smooth";
    case Filter::Crt:
        return "crt";
    }
    return "sharp";
}

std::optional<BackendKind> parseBackendKind(std::string_view text) {
    text = str::trim(text);
    if (str::iequals(text, "auto"))
        return BackendKind::Auto;
    if (str::iequals(text, "vulkan") || str::iequals(text, "vk"))
        return BackendKind::Vulkan;
    if (str::iequals(text, "opengl") || str::iequals(text, "gl"))
        return BackendKind::OpenGL;
    return std::nullopt;
}

std::optional<Aspect> parseAspect(std::string_view text) {
    text = str::trim(text);
    if (str::iequals(text, "original") || text == "4:3")
        return Aspect::Original;
    if (str::iequals(text, "stretch"))
        return Aspect::Stretch;
    if (str::iequals(text, "integer"))
        return Aspect::Integer;
    return std::nullopt;
}

std::optional<Filter> parseFilter(std::string_view text) {
    text = str::trim(text);
    for (Filter f : {Filter::Sharp, Filter::Nearest, Filter::Smooth, Filter::Crt})
        if (str::iequals(text, toString(f)))
            return f;
    return std::nullopt;
}

std::string_view displayName(Aspect aspect) {
    switch (aspect) {
    case Aspect::Original:
        return "4:3";
    case Aspect::Stretch:
        return "Stretch";
    case Aspect::Integer:
        return "Integer scale";
    }
    return "4:3";
}

std::string_view displayName(Filter filter) {
    switch (filter) {
    case Filter::Sharp:
        return "Sharp";
    case Filter::Nearest:
        return "Nearest";
    case Filter::Smooth:
        return "Smooth";
    case Filter::Crt:
        return "CRT";
    }
    return "Sharp";
}

Aspect nextAspect(Aspect aspect) {
    return static_cast<Aspect>((static_cast<int>(aspect) + 1) % kAspectCount);
}

Filter nextFilter(Filter filter) {
    return static_cast<Filter>((static_cast<int>(filter) + 1) % kFilterCount);
}

} // namespace brc::render
