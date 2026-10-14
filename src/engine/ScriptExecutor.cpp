#include "engine/ScriptExecutor.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <format>

namespace brc::engine {
namespace {

using formats::ScriptSymbol;

std::uint32_t floatBits(float f) { return std::bit_cast<std::uint32_t>(f); }

// _ftol: truncation toward zero, saturating where x87 would produce the
// "integer indefinite" value.
int ftol(float f) {
    if (std::isnan(f) || f >= 2147483648.0f || f < -2147483648.0f)
        return INT32_MIN;
    return static_cast<int>(f);
}

} // namespace

float ScriptArg::f() const { return std::bit_cast<float>(raw); }
int ScriptArg::truncated() const { return ftol(f()); }

ScriptExecutor::ScriptExecutor(formats::ScriptProgram program)
    : m_chunk(std::move(program.chunk)), m_end(program.codeEnd) {
    m_symbols.reserve(program.symbols.size());
    for (auto& s : program.symbols)
        m_symbols.push_back(Symbol{s.type, std::move(s.name), 0.0f, {}});
    if (m_end > m_chunk.size())
        m_end = m_chunk.size();
    m_running = m_pc < m_end;
}

int ScriptExecutor::findSymbol(std::string_view name) const {
    for (std::size_t i = 0; i < m_symbols.size(); ++i)
        if (m_symbols[i].name == name)
            return static_cast<int>(i);
    return -1;
}

const ScriptExecutor::Symbol* ScriptExecutor::symbol(int id) const {
    if (id < 0 || static_cast<std::size_t>(id) >= m_symbols.size())
        return nullptr;
    return &m_symbols[static_cast<std::size_t>(id)];
}

void ScriptExecutor::setFloat(int id, float value) {
    if (id < 0 || static_cast<std::size_t>(id) >= m_symbols.size())
        return;
    auto& s = m_symbols[static_cast<std::size_t>(id)];
    s.number = value;
    s.type = ScriptSymbol::Type::Float;
}

void ScriptExecutor::setString(int id, std::string_view value) {
    if (id < 0 || static_cast<std::size_t>(id) >= m_symbols.size())
        return;
    auto& s = m_symbols[static_cast<std::size_t>(id)];
    s.text.assign(value.substr(0, 31));
    s.type = ScriptSymbol::Type::StringVar;
}

std::string_view ScriptExecutor::text(int id) const {
    const Symbol* s = symbol(id);
    if (!s)
        return {};
    return s->type == ScriptSymbol::Type::StringConst ? std::string_view(s->name) : std::string_view(s->text);
}

// --- value stack (stack.cpp) -------------------------------------------------

void ScriptExecutor::push(std::uint32_t raw) {
    if (m_sp == m_stack.size()) {
        if (m_host)
            m_host->log("script stack overflow!");
        return; // the value is dropped
    }
    m_stack[m_sp++] = raw;
}

void ScriptExecutor::pushF(float f) { push(floatBits(f)); }

void ScriptExecutor::pushS(std::int16_t s) {
    // A 16-bit push writes only the low half of the slot.
    if (m_sp == m_stack.size()) {
        if (m_host)
            m_host->log("script stack overflow!");
        return;
    }
    auto& slot = m_stack[m_sp++];
    slot = (slot & 0xFFFF0000u) | static_cast<std::uint16_t>(s);
}

std::uint32_t ScriptExecutor::pop() {
    if (m_sp == 0) {
        if (m_host)
            m_host->log("script stack underflow!");
        return 0; // the original leaves its destination uninitialised
    }
    return m_stack[--m_sp];
}

float ScriptExecutor::popF() { return std::bit_cast<float>(pop()); }
std::int16_t ScriptExecutor::popS() { return static_cast<std::int16_t>(pop() & 0xFFFF); }

// --- decoding ----------------------------------------------------------------

bool ScriptExecutor::readI16(std::int16_t& out) {
    if (m_pc + 2 > m_chunk.size())
        return false;
    out = static_cast<std::int16_t>(std::to_integer<unsigned>(m_chunk[m_pc]) |
                                    std::to_integer<unsigned>(m_chunk[m_pc + 1]) << 8);
    m_pc += 2;
    return true;
}

bool ScriptExecutor::readF32(float& out) {
    if (m_pc + 4 > m_chunk.size())
        return false;
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i)
        v |= std::to_integer<std::uint32_t>(m_chunk[m_pc + static_cast<std::size_t>(i)]) << (8 * i);
    out = std::bit_cast<float>(v);
    m_pc += 4;
    return true;
}

void ScriptExecutor::invalid(std::int16_t opcode, ScriptBuiltins& host) {
    host.log(std::format("Bad error: unknown opcode {}.", opcode));
    m_pc = m_end;
}

void ScriptExecutor::run(ScriptBuiltins& host) {
    if (!m_running)
        return;
    m_host = &host;
    m_yield = false;
    while (m_pc < m_end && !m_yield) {
        std::int16_t opcode = 0;
        if (!readI16(opcode)) {
            m_pc = m_end;
            break;
        }
        execute(opcode, host);
    }
    if (m_pc >= m_end)
        m_running = false;
    m_host = nullptr;
}

void ScriptExecutor::execute(std::int16_t opcode, ScriptBuiltins& host) {
    namespace op = formats::op;
    auto binary = [&](auto fn) {
        const float b = popF();
        const float a = popF();
        pushF(fn(a, b));
    };
    auto truth = [](bool v) { return v ? 1.0f : 0.0f; };

    switch (opcode) {
    case op::kPushF: {
        float f = 0;
        if (!readF32(f))
            return invalid(opcode, host);
        pushF(f);
        return;
    }
    case op::kPushI:
    case op::kPushSym: {
        std::int16_t v = 0;
        if (!readI16(v))
            return invalid(opcode, host);
        pushS(v);
        return;
    }
    case op::kLoad: {
        std::int16_t id = 0;
        if (!readI16(id))
            return invalid(opcode, host);
        // Pushes the symbol's 4-byte value field: a float variable's value
        // (the compiler never loads strings).
        const Symbol* s = symbol(id);
        pushF(s ? s->number : 0.0f);
        return;
    }
    case op::kPop:
    case op::kPop2:
    case op::kPopS:
        pop();
        return;
    case 0x06:
    case 0x07:
        return; // no-ops
    case op::kJz: {
        const std::int16_t target = popS();
        const float cond = popF();
        if (cond == 0.0f)
            m_pc = static_cast<std::uint16_t>(target); // offset from the chunk start
        return;
    }
    case op::kJmp:
        m_pc = static_cast<std::uint16_t>(popS());
        return;
    case op::kAssign: {
        const std::int16_t id = popS();
        const float v = popF();
        setFloat(id, v);
        pushF(v);
        return;
    }
    case op::kAdd: return binary([](float a, float b) { return a + b; });
    case op::kSub: return binary([](float a, float b) { return a - b; });
    case op::kMul: return binary([](float a, float b) { return a * b; });
    case op::kDiv: return binary([](float a, float b) { return a / b; });
    case op::kMod:
        return binary([&](float a, float b) {
            const int ib = ftol(b);
            if (ib == 0) {
                host.log("script: modulo by zero"); // the original crashes here
                return 0.0f;
            }
            return static_cast<float>(ftol(a) % ib);
        });
    case op::kNeg: pushF(-popF()); return;
    case op::kPow: return binary([](float a, float b) { return static_cast<float>(std::pow(double{a}, double{b})); });
    case op::kRand: {
        const int n = ftol(popF());
        pushF(n > 0 ? static_cast<float>(host.random() % n + 1) : 1.0f);
        return;
    }
    case op::kSin: pushF(static_cast<float>(std::sin(double{popF()}))); return;
    case op::kCos: pushF(static_cast<float>(std::cos(double{popF()}))); return;
    case op::kSqrt: pushF(static_cast<float>(std::sqrt(double{popF()}))); return;
    case op::kEq: return binary([&](float a, float b) { return truth(a == b); });
    case op::kNe: return binary([&](float a, float b) { return truth(a != b); });
    case op::kOr: return binary([&](float a, float b) { return truth(a != 0.0f || b != 0.0f); });
    case op::kAnd: return binary([&](float a, float b) { return truth(a != 0.0f && b != 0.0f); });
    case op::kNot: pushF(truth(popF() == 0.0f)); return;
    case op::kGt: return binary([&](float a, float b) { return truth(a > b); });
    case op::kGe: return binary([&](float a, float b) { return truth(a >= b); });
    case op::kLt: return binary([&](float a, float b) { return truth(a < b); });
    case op::kLe: return binary([&](float a, float b) { return truth(a <= b); });
    case op::kSAssign: {
        const std::int16_t dst = popS();
        const std::int16_t src = popS();
        const std::string value(text(src));
        setString(dst, value);
        pushS(dst);
        return;
    }
    case op::kSConcat: {
        const std::int16_t dst = popS();
        const std::int16_t b = popS();
        const std::int16_t a = popS();
        std::string value(text(a).substr(0, 31));
        value += text(b);
        setString(dst, value);
        pushS(dst);
        return;
    }
    case op::kNum2Str: {
        const std::int16_t dst = popS();
        const float f = popF();
        // sprintf("%-*f", 31, f), then trailing '0'/' ' and one '.' removed.
        char buf[64];
        std::snprintf(buf, sizeof buf, "%-*f", 31, static_cast<double>(f));
        std::string s(buf, std::min<std::size_t>(std::strlen(buf), 31));
        while (!s.empty() && (s.back() == '0' || s.back() == ' '))
            s.pop_back();
        if (!s.empty() && s.back() == '.')
            s.pop_back();
        setString(dst, s);
        pushS(dst);
        return;
    }
    case op::kStrCmp: {
        const std::int16_t b = popS();
        const std::int16_t a = popS();
        const auto ta = text(a), tb = text(b);
        const int c = std::memcmp(ta.data(), tb.data(), std::min(ta.size(), tb.size()));
        const int r = c != 0 ? c : (ta.size() < tb.size() ? -1 : ta.size() > tb.size() ? 1 : 0);
        pushF(r < 0 ? -1.0f : r > 0 ? 1.0f : 0.0f);
        return;
    }
    case op::kStrICmp: {
        const std::int16_t b = popS();
        const std::int16_t a = popS();
        const auto ta = text(a), tb = text(b);
        // CRT _strcmpi: compares lowercased bytes, returns their difference.
        int r = 0;
        for (std::size_t i = 0;; ++i) {
            const int ca = i < ta.size() ? std::tolower(static_cast<unsigned char>(ta[i])) : 0;
            const int cb = i < tb.size() ? std::tolower(static_cast<unsigned char>(tb[i])) : 0;
            if (ca != cb || ca == 0) {
                r = ca - cb;
                break;
            }
        }
        pushF(static_cast<float>(r));
        return;
    }
    default:
        break;
    }

    const char* sig = formats::builtinSignature(opcode);
    if (!sig)
        return invalid(opcode, host);
    // Pop in reverse push order, then call with the arguments in push order.
    const std::size_t n = std::strlen(sig);
    std::array<ScriptArg, 16> args{};
    for (std::size_t i = n; i-- > 0;)
        args[i].raw = sig[i] == 's' ? (pop() & 0xFFFF) : pop();
    if (host.builtin(*this, opcode, std::span(args.data(), n)))
        m_yield = true;
}

} // namespace brc::engine
