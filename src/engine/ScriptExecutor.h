#pragma once

#include "formats/Script.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace brc::engine {

// One argument of a built-in: the raw 4-byte stack slot, read as the kind the
// opcode's signature says ('f' float or 's' 16-bit symbol index).
struct ScriptArg {
    std::uint32_t raw = 0;

    float f() const;
    int truncated() const; // _ftol: float to int, toward zero
    std::int16_t sym() const { return static_cast<std::int16_t>(raw & 0xFFFF); }
};

class ScriptExecutor;

// What the executor needs from its owner (the HCScript): the built-in
// opcodes, random numbers and logging.
class ScriptBuiltins {
public:
    virtual ~ScriptBuiltins() = default;
    // Executes built-in `opcode` with its arguments in push order. Return true
    // to yield (the script waits); the executor continues after the built-in
    // on the next run().
    virtual bool builtin(ScriptExecutor& ex, std::int16_t opcode, std::span<const ScriptArg> args) = 0;
    // MSVC rand(): 0..32767.
    virtual int random() = 0;
    virtual void log(std::string_view message) = 0;
};

// The bytecode interpreter of compiled Human Code scripts (exec.cpp, run loop
// 0x445a10; value stack in stack.cpp). See docs/engine/scripts.md.
class ScriptExecutor {
public:
    struct Symbol {
        formats::ScriptSymbol::Type type;
        std::string name;  // variable name or literal text
        float number = 0;  // value of a float variable
        std::string text;  // value of a string variable (at most 31 characters)
    };

    explicit ScriptExecutor(formats::ScriptProgram program);

    // Runs until a built-in yields, the code ends or an invalid opcode is met.
    void run(ScriptBuiltins& host);
    bool running() const { return m_running; }

    // Symbols. Lookups from C++ are case-sensitive (FindSymbol 0x4477d0).
    int findSymbol(std::string_view name) const;
    std::size_t symbolCount() const { return m_symbols.size(); }
    const Symbol* symbol(int id) const;
    // SetFloat (0x45bb30) / SetString (0x45bac0) also change the symbol's type.
    void setFloat(int id, float value);
    void setString(int id, std::string_view value);
    // "Text of a symbol": the literal for string constants, the value
    // otherwise (for a float variable that is its zeroed string field).
    std::string_view text(int id) const;

    std::size_t pc() const { return m_pc; }
    std::size_t stackDepth() const { return m_sp; }

private:
    void push(std::uint32_t raw);
    void pushF(float f);
    void pushS(std::int16_t s);
    std::uint32_t pop();
    float popF();
    std::int16_t popS();
    bool readI16(std::int16_t& out);
    bool readF32(float& out);
    void execute(std::int16_t opcode, ScriptBuiltins& host);
    void invalid(std::int16_t opcode, ScriptBuiltins& host);

    std::vector<std::byte> m_chunk;
    std::size_t m_pc = 8;
    std::size_t m_end = 8;
    std::vector<Symbol> m_symbols;
    std::array<std::uint32_t, 30> m_stack{};
    std::size_t m_sp = 0;
    bool m_running = true;
    bool m_yield = false;
    ScriptBuiltins* m_host = nullptr; // during run(), for stack messages
};

} // namespace brc::engine
