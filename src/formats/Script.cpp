#include "formats/Script.h"

#include "formats/ByteReader.h"

#include <format>

namespace brc::formats {

std::expected<ScriptProgram, std::string> parseScript(std::span<const std::byte> chunk) {
    // Executor constructor 0x445820, symbol loader 0x45bb50.
    LeReader r(chunk);
    std::uint32_t version = 0, symoff = 0;
    if (!r.u32(version) || !r.u32(symoff))
        return std::unexpected("Script executor error: truncated header");
    if (version != kScriptVersion)
        return std::unexpected(std::format(
            "Script executor error: executor version {}, script compiled with compiler version {}", kScriptVersion,
            version));
    if (symoff < 8 || !r.seek(symoff))
        return std::unexpected("Script executor error: error loading symbol table");

    ScriptProgram p;
    p.codeEnd = symoff;
    std::uint32_t nsym = 0;
    if (!r.u32(nsym) || nsym > 65535)
        return std::unexpected("Script executor error: error loading symbol table");
    p.symbols.resize(nsym);
    std::vector<bool> seen(nsym);
    for (std::uint32_t i = 0; i < nsym; ++i) {
        std::uint32_t id = 0, type = 0, len = 0;
        if (!r.u32(id) || !r.u32(type) || !r.u32(len) || id >= nsym || seen[id] || type > 2 || len > r.remaining())
            return std::unexpected("Script executor error: error loading symbol table");
        seen[id] = true;
        auto& sym = p.symbols[id];
        sym.type = static_cast<ScriptSymbol::Type>(type);
        sym.name.assign(reinterpret_cast<const char*>(r.rest().data()), len);
        // The executor copies the text into a 32-byte field.
        if (sym.name.size() > 31)
            sym.name.resize(31);
        r.skip(len);
    }
    p.chunk.assign(chunk.begin(), chunk.end());
    return p;
}

const char* builtinSignature(std::int16_t opcode) {
    // Pop counts of every built-in handler (0x446590, 0x446f20, 0x446c50,
    // 0x447280), including the unimplemented ones, which still pop their
    // arguments. See docs/formats/script.md.
    switch (opcode) {
    case 0x2b: return "fsf";
    case 0x2c: return "fsfff";
    case 0x2d: return "fsfff";
    case 0x2e: return "fsfs";
    case 0x2f: return "fsfff";
    case 0x30: return "fsfsff";
    case 0x31: return "sffs";
    case 0x32: return "s";
    case 0x33: return "s";
    case 0x34: return "ssffff";
    case 0x35: return "ssfff";
    case 0x36: return "sfff";
    case 0x37: return "";
    case 0x38: return "sfff";
    case 0x39: return "sfff";
    case 0x3c: return "fsffff";
    case 0x3d: return "ff";
    case 0x3e: return "fsffff";
    case 0x3f: return "s";
    case 0x40: return "s";
    case 0x41: return "f";
    case 0x42: return "";
    case 0x43: return "fsssf";
    case 0x44: return "s";
    case 0x45: return "";
    case 0x48: return "s";
    case 0x49:
    case 0x4a:
    case 0x4b:
    case 0x4c:
    case 0x4d: return "sf";
    case 0x4e: return "s";
    case 0x4f: return "ssff"; // its handler falls through into 0x50's (inferred: unused on the disc)
    case 0x50:
    case 0x51: return "s";
    case 0x54: return "ff";
    case 0x55: return "f";
    case 0x56: return "fs";
    case 0x57: return "s";
    case 0x58: return "s";
    case 0x59: return "";
    case 0x5a: return "sf";
    case 0x5b: return "f";
    case 0x5c: return "f";
    case 0x5d: return "s";
    case 0x5e: return "f";
    case 0x5f: return "f";
    case 0x60: return "s";
    case 0x61: return "f";
    case 0x62:
    case 0x63: return "s";
    case 0x64: return "fsffffff";
    case 0x65: return "sffffffff";
    default: return nullptr;
    }
}

} // namespace brc::formats
