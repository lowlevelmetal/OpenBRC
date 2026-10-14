#pragma once

#include "engine/Broadcaster.h"
#include "engine/ScriptExecutor.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>

namespace brc::engine {

class Anim;
class Engine;
class Node;

// HCScript (hcscript.cpp, ctor 0x458fb0): one loaded script with its executor,
// its wait state and the built-ins. See docs/engine/scripts.md.
class Script final : public Listener, private ScriptBuiltins {
public:
    Script(Engine& engine, std::string name, ScriptExecutor executor);
    ~Script() override;

    const std::string& name() const { return m_name; }
    bool ended() const { return m_ended; }
    bool started() const { return m_started; }
    bool waiting() const { return m_waiting; }
    bool killWithNode() const { return m_killWithNode; }
    ScriptExecutor& executor() { return m_exec; }

    void start(Node& node);   // 0x459510
    void resume(Node& node);  // 0x459580
    void preempt(Node& node); // 0x4595f0

    void listen(int message, const MessageData& data) override; // 0x4586b0

private:
    enum class Wait { None, Timer = 0x65, Node = 0x69, Sound = 0x6a, Anim = 0x6c, Script = 0x6d };

    void execute(Node& node); // 0x458640
    bool builtin(ScriptExecutor& ex, std::int16_t opcode, std::span<const ScriptArg> args) override;
    int random() override;
    void log(std::string_view message) override;
    void waitFor(Wait type, const void* object = nullptr, int seq = 0, std::uint32_t deadline = 0);
    void waitAnim(std::string_view name, int seq);
    void waitSound(std::string_view name);
    void waitScript(std::string_view name);

    Engine& m_engine;
    std::string m_name;
    ScriptExecutor m_exec;
    Node* m_node = nullptr;     // +0x20: the node of the current run slice
    int m_counter = 1;          // +0x14
    bool m_started = false;     // +0x10
    bool m_ended = false;       // +0x52
    bool m_waiting = false;     // +0x54
    bool m_paused = false;      // +0x68
    std::uint32_t m_pausedAt = 0;
    bool m_killWithNode = true; // +0x50
    Wait m_wait = Wait::None;
    const void* m_waitObject = nullptr; // anim, sound, script or node-name identity
    std::string m_waitName;
    int m_waitSeq = 0;
    std::uint32_t m_deadline = 0;
};

// The global script map (0x80a420) and the HCNode script API.
class ScriptSystem {
public:
    explicit ScriptSystem(Engine& engine) : m_engine(engine) {}
    ~ScriptSystem();

    bool load(Node& node, std::string_view name);      // 0x4558b0
    bool start(Node& node, std::string_view name);     // 0x455b40
    bool runScript(Node& node, std::string_view name); // 0x455890: load + start
    bool stopScript(Node& node, std::string_view name); // 0x455c10
    void stopAll(Node& node);                          // 0x455bb0
    bool isRunning(std::string_view name) const;       // 0x455840
    Script* find(std::string_view name) const;
    bool setVar(std::string_view script, std::string_view var, std::string_view value); // 0x455cb0
    bool setVar(std::string_view script, std::string_view var, double value);           // 0x455d60

    void deleteEnded();               // node tick, step 2
    void deleteForNode(Node& node);   // HCNode destructor: kill-with-node scripts
    void deleteAll();                 // 0x455730

    int rand(); // the engine's MSVC rand()

private:
    Engine& m_engine;
    std::map<std::string, std::unique_ptr<Script>> m_scripts; // key: lower-case name
};

} // namespace brc::engine
