#pragma once

#include "render/Backend.h"

#include <filesystem>
#include <string>

namespace brc::render {

// Writes a captured output image: BMP when the extension is ".bmp", PNG otherwise.
bool saveCapture(const Capture& capture, const std::filesystem::path& path, std::string* error);

} // namespace brc::render
