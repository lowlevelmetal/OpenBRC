#pragma once

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace brc::formats {

// A compiled Human Code script ('SCR ' chunk, compiler version 5). All fields
// are little-endian. The bytecode is kept as a copy of the whole chunk because
// jump targets are offsets from the chunk start (and one script on the disc
// jumps into its own header). See docs/formats/script.md.
struct ScriptSymbol {
    enum class Type : std::uint32_t { Float = 0, StringConst = 1, StringVar = 2 };
    Type type = Type::Float;
    std::string name; // variable name, or the literal text of a string constant
};

struct ScriptProgram {
    std::vector<std::byte> chunk; // the whole chunk
    std::uint32_t codeEnd = 8;    // `symoff`: code is [8, codeEnd)
    std::vector<ScriptSymbol> symbols; // indexed by symbol id
};

inline constexpr std::uint32_t kScriptVersion = 5;

// Fails (with a message like the original's "Script executor error: ...") on a
// wrong version or a malformed symbol table.
std::expected<ScriptProgram, std::string> parseScript(std::span<const std::byte> chunk);

namespace op {
// Opcode numbers (16-bit, little-endian). Names are ours.
inline constexpr std::int16_t kPushF = 0x01, kPushI = 0x02, kPushSym = 0x03, kLoad = 0x04, kPop = 0x05,
                              kJz = 0x08, kJmp = 0x09, kPop2 = 0x0a, kPopS = 0x0b, kAssign = 0x0d,
                              kAdd = 0x0e, kSub = 0x0f, kMul = 0x10, kDiv = 0x11, kMod = 0x12, kNeg = 0x13,
                              kPow = 0x14, kRand = 0x15, kSin = 0x16, kCos = 0x17, kSqrt = 0x18, kEq = 0x19,
                              kNe = 0x1a, kOr = 0x1b, kAnd = 0x1c, kNot = 0x1d, kGt = 0x1e, kGe = 0x1f,
                              kLt = 0x20, kLe = 0x21, kSAssign = 0x24, kSConcat = 0x25, kNum2Str = 0x26,
                              kStrCmp = 0x27, kStrICmp = 0x28;
inline constexpr std::int16_t kPlayAnim = 0x2b, kPlayAnimAt = 0x2c, kWaitAnim = 0x31, kHideAnim = 0x32,
                              kKillAnim = 0x33, kCopyAnim = 0x35, kMoveAnim = 0x38, kPlaceAnim = 0x39,
                              kPlaySound = 0x3e, kWaitSound = 0x3f, kStopSound = 0x40, kStopChannel = 0x41,
                              kStopAllSounds = 0x42, kRunScript = 0x56, kWaitScript = 0x57, kStopScript = 0x58,
                              kStopAllScripts = 0x59, kSetKillWithNode = 0x5b, kSetFlag36 = 0x5c,
                              kNodeMessage = 0x5d, kNodeNumber = 0x5e, kWait = 0x5f, kGotoNode = 0x60,
                              kPauseNode = 0x61;
} // namespace op

// Argument signature of a built-in opcode (0x2b..0x65), in push order:
// 'f' = float slot, 's' = 16-bit symbol slot. nullptr for invalid opcodes.
const char* builtinSignature(std::int16_t opcode);

} // namespace brc::formats
