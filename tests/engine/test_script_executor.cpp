#include "TestData.h"

#include "engine/ScriptExecutor.h"
#include "formats/OmtFile.h"
#include "vfs/DiscManifest.h"

#include <gtest/gtest.h>

#include <cstring>
#include <format>

namespace brc {
namespace {

using engine::ScriptArg;
using engine::ScriptExecutor;
namespace op = formats::op;

struct Call {
    std::int16_t opcode;
    std::vector<ScriptArg> args;
};

// Records built-in calls; waits yield; nodeMessage("initDelay") answers like
// the game-node base (0x41dcd0) by setting `delay`.
class MockHost : public engine::ScriptBuiltins {
public:
    bool builtin(ScriptExecutor& ex, std::int16_t opcode, std::span<const ScriptArg> args) override {
        calls.push_back({opcode, {args.begin(), args.end()}});
        if (opcode == op::kNodeMessage) {
            messages.emplace_back(ex.text(args[0].sym()));
            if (messages.back() == "initDelay")
                ex.setFloat(ex.findSymbol("delay"), 500.0f);
        }
        return opcode == op::kWait || opcode == op::kGotoNode ||
               ((opcode == op::kPlayAnim || opcode == op::kPlayAnimAt || opcode == op::kPlaySound ||
                 opcode == op::kRunScript) &&
                args[0].f() != 0.0f);
    }
    int random() override { return 0; }
    void log(std::string_view m) override { logs.emplace_back(m); }

    std::vector<Call> calls;
    std::vector<std::string> messages;
    std::vector<std::string> logs;
};

std::vector<std::byte> le(std::initializer_list<int> v) {
    std::vector<std::byte> out;
    for (int b : v)
        out.push_back(static_cast<std::byte>(b));
    return out;
}

// Builds a version-5 chunk from code bytes and string-constant symbols.
std::vector<std::byte> makeChunk(std::vector<std::byte> code, std::vector<std::pair<int, std::string>> syms) {
    std::vector<std::byte> c;
    auto u32 = [&](std::uint32_t v) {
        for (int i = 0; i < 4; ++i)
            c.push_back(static_cast<std::byte>(v >> (8 * i)));
    };
    u32(5);
    u32(static_cast<std::uint32_t>(8 + code.size()));
    c.insert(c.end(), code.begin(), code.end());
    u32(static_cast<std::uint32_t>(syms.size()));
    for (std::size_t i = 0; i < syms.size(); ++i) {
        u32(static_cast<std::uint32_t>(i));
        u32(static_cast<std::uint32_t>(syms[i].first));
        u32(static_cast<std::uint32_t>(syms[i].second.size()));
        for (char ch : syms[i].second)
            c.push_back(static_cast<std::byte>(ch));
    }
    return c;
}

TEST(ScriptExecutor, RejectsWrongVersion) {
    auto chunk = makeChunk({}, {});
    chunk[0] = std::byte{4};
    EXPECT_FALSE(formats::parseScript(chunk));
}

// Minimal assembler for test programs (offsets from the chunk start, code at 8).
struct Asm {
    std::vector<std::byte> code;
    std::size_t here() const { return 8 + code.size(); }
    void u16(int v) {
        code.push_back(static_cast<std::byte>(v & 0xFF));
        code.push_back(static_cast<std::byte>((v >> 8) & 0xFF));
    }
    void op(std::int16_t o) { u16(o); }
    void pushf(float f) {
        op(op::kPushF);
        std::uint32_t v;
        std::memcpy(&v, &f, 4);
        for (int i = 0; i < 4; ++i)
            code.push_back(static_cast<std::byte>(v >> (8 * i)));
    }
    void pushi(int v) { op(op::kPushI), u16(v); }
    void pushsym(int v) { op(op::kPushSym), u16(v); }
    void load(int v) { op(op::kLoad), u16(v); }
    std::size_t pushiPatch() {
        op(op::kPushI);
        u16(0);
        return code.size() - 2;
    }
    void patch(std::size_t at, std::size_t target) {
        code[at] = static_cast<std::byte>(target & 0xFF);
        code[at + 1] = static_cast<std::byte>(target >> 8);
    }
};

TEST(ScriptExecutor, ArithmeticComparisonsAndJumps) {
    // x = 2 + 3; if (x >= 5) nodeMessage("yes") else nodeMessage("no")
    Asm a;
    a.pushf(2), a.pushf(3), a.op(op::kAdd), a.pushsym(0), a.op(op::kAssign), a.op(op::kPop);
    a.load(0), a.pushf(5), a.op(op::kGe);
    const auto toElse = a.pushiPatch();
    a.op(op::kJz);
    a.pushsym(1), a.op(op::kNodeMessage);
    const auto toEnd = a.pushiPatch();
    a.op(op::kJmp);
    a.patch(toElse, a.here());
    a.pushsym(2), a.op(op::kNodeMessage);
    a.patch(toEnd, a.here());
    auto p = formats::parseScript(makeChunk(a.code, {{0, "x"}, {1, "yes"}, {1, "no"}}));
    ASSERT_TRUE(p) << p.error();
    ScriptExecutor ex(std::move(*p));
    MockHost host;
    ex.run(host);
    EXPECT_FALSE(ex.running());
    ASSERT_EQ(host.messages.size(), 1u);
    EXPECT_EQ(host.messages[0], "yes");
    EXPECT_EQ(ex.symbol(0)->number, 5.0f);
    EXPECT_EQ(ex.stackDepth(), 0u);
}

TEST(ScriptExecutor, StackOverflowDropsAndUnderflowLogs) {
    Asm a;
    for (int i = 0; i < 31; ++i)
        a.pushf(1);
    for (int i = 0; i < 31; ++i)
        a.op(op::kPop);
    auto p = formats::parseScript(makeChunk(a.code, {}));
    ASSERT_TRUE(p);
    ScriptExecutor ex(std::move(*p));
    MockHost host;
    ex.run(host);
    ASSERT_EQ(host.logs.size(), 2u);
    EXPECT_EQ(host.logs[0], "script stack overflow!");
    EXPECT_EQ(host.logs[1], "script stack underflow!");
}

TEST(ScriptExecutor, Num2StrTrimsLikeTheOriginal) {
    Asm a;
    a.pushf(2.5f), a.pushsym(0), a.op(op::kNum2Str), a.op(op::kPop);
    a.pushf(3.0f), a.pushsym(1), a.op(op::kNum2Str), a.op(op::kPop);
    auto p = formats::parseScript(makeChunk(a.code, {{2, "s"}, {2, "t"}}));
    ASSERT_TRUE(p);
    ScriptExecutor ex(std::move(*p));
    MockHost host;
    ex.run(host);
    EXPECT_EQ(ex.text(0), "2.5");
    EXPECT_EQ(ex.text(1), "3");
}

TEST(ScriptExecutor, InvalidOpcodeEndsTheScript) {
    auto p = formats::parseScript(makeChunk(le({0x22, 0, 0x03, 0, 0, 0, 0x5d, 0}), {{1, "never"}}));
    ASSERT_TRUE(p);
    ScriptExecutor ex(std::move(*p));
    MockHost host;
    ex.run(host);
    EXPECT_FALSE(ex.running());
    EXPECT_TRUE(host.messages.empty());
    ASSERT_EQ(host.logs.size(), 1u);
    EXPECT_EQ(host.logs[0], "Bad error: unknown opcode 34.");
}

std::optional<formats::ScriptProgram> loadDiscScript(std::string_view omtPath, std::string_view name) {
    auto omt = formats::OmtFile::open(test::gameData()->open(omtPath), std::string(omtPath));
    if (!omt)
        return std::nullopt;
    const auto* c = omt->find(formats::chunk::kScript, name);
    if (!c)
        return std::nullopt;
    auto p = formats::parseScript(omt->read(*c));
    if (!p)
        return std::nullopt;
    return std::move(*p);
}

TEST(ScriptExecutor, DelayAsksTheNodeThenWaits) {
    BRC_REQUIRE_GAME_DATA();
    auto p = loadDiscScript("resources/common.omt", "Delay");
    ASSERT_TRUE(p);
    ScriptExecutor ex(std::move(*p));
    MockHost host;
    ex.run(host);
    ASSERT_EQ(host.calls.size(), 2u);
    EXPECT_EQ(host.calls[0].opcode, op::kNodeMessage);
    EXPECT_EQ(host.messages[0], "initDelay");
    EXPECT_EQ(host.calls[1].opcode, op::kWait);
    EXPECT_EQ(host.calls[1].args[0].f(), 500.0f);
}

TEST(ScriptExecutor, EveryDiscScriptRuns) {
    BRC_REQUIRE_GAME_DATA();
    int scripts = 0;
    for (const auto& e : vfs::kDiscManifest) {
        if (!e.path.ends_with(".omt"))
            continue;
        auto omt = formats::OmtFile::open(test::gameData()->open(e.path), std::string(e.path));
        ASSERT_TRUE(omt);
        for (const auto* c : omt->ofType(formats::chunk::kScript)) {
            auto p = formats::parseScript(omt->read(*c));
            ASSERT_TRUE(p) << e.path << " " << c->name << ": " << p.error();
            ScriptExecutor ex(std::move(*p));
            MockHost host;
            // Waits yield; resume a bounded number of times (some scripts loop forever).
            for (int slice = 0; slice < 200 && ex.running(); ++slice)
                ex.run(host);
            for (const auto& l : host.logs) {
                const bool pagerDelayQuirk = c->name == "PagerDelay";
                EXPECT_TRUE(pagerDelayQuirk) << e.path << " " << c->name << ": " << l;
            }
            ++scripts;
        }
    }
    EXPECT_EQ(scripts, 54);
}

} // namespace
} // namespace brc
