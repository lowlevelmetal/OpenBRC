#include "engine/Scripts.h"

#include "core/Log.h"
#include "core/StringUtil.h"
#include "engine/Anim.h"
#include "engine/Engine.h"
#include "engine/Node.h"
#include "engine/SoundManager.h"

#include <format>

namespace brc::engine {

namespace op = formats::op;

Script::Script(Engine& engine, std::string name, ScriptExecutor executor)
    : m_engine(engine), m_name(std::move(name)), m_exec(std::move(executor)) {
    m_engine.scriptEvents().addListener(this);
}

Script::~Script() = default;

int Script::random() { return m_engine.rand(); }

void Script::log(std::string_view message) { log::info("script {}: {}", m_name, message); }

// --- running --------------------------------------------------------------------------

void Script::execute(Node& node) {
    // 0x458640.
    m_node = &node;
    if (!m_ended && !m_waiting && !m_paused && m_exec.running()) {
        m_waiting = false;
        m_exec.run(*this);
    }
    if (!m_exec.running() && !m_waiting)
        m_ended = true;
}

void Script::start(Node& node) {
    // 0x459510.
    if (!m_ended && !m_started) {
        m_started = true;
        while (!m_ended) {
            if (m_waiting)
                break;
            execute(node);
            if (!m_waiting)
                ++m_counter;
        }
    }
    if (m_ended)
        node.scriptEnded(*this);
}

void Script::resume(Node& node) {
    // 0x459580.
    if (!m_started)
        return;
    if (!m_ended) {
        while (!m_ended) {
            if (m_waiting || m_paused)
                break;
            execute(node);
            if (!m_waiting)
                ++m_counter;
        }
    }
    if (m_ended)
        node.scriptEnded(*this);
}

void Script::preempt(Node& node) {
    // 0x4595f0. The executor is left alone: a script that stops itself runs
    // on until its next wait and then also ends.
    if (!m_ended) {
        m_ended = true;
        m_waiting = false;
        node.scriptPreempted(*this);
    }
}

void Script::listen(int message, const MessageData& data) {
    // 0x4586b0.
    const std::uint32_t now = m_engine.ticks();
    if (message == msg::kPaused && !m_paused) {
        m_paused = true;
        m_pausedAt = now;
    } else if (message == msg::kResumed && m_paused) {
        if (m_waiting && m_wait == Wait::Timer)
            m_deadline += now - m_pausedAt;
        m_paused = false;
    }
    if (m_waiting && !m_paused) {
        bool wake = false;
        switch (m_wait) {
        case Wait::Timer:
            wake = now >= m_deadline; // on any message
            break;
        case Wait::Node:
            wake = message == msg::kNodeEntered && data.ptr &&
                   str::iequals(static_cast<const char*>(data.ptr), m_waitName);
            break;
        case Wait::Sound:
            wake = message == msg::kSoundDone && data.extra == m_waitObject;
            break;
        case Wait::Anim:
            wake = message == msg::kAnimDone && data.ptr == m_waitObject && data.value == m_waitSeq;
            break;
        case Wait::Script:
            wake = message == msg::kScriptDone && data.ptr == m_waitObject;
            break;
        case Wait::None:
            break;
        }
        if (wake) {
            m_waiting = false;
            m_wait = Wait::None;
            ++m_counter;
        }
    }
    if (!m_waiting && !m_ended && m_started && !m_paused && message == msg::kTick && data.ptr)
        resume(*static_cast<Node*>(const_cast<void*>(data.ptr)));
}

void Script::waitFor(Wait type, const void* object, int seq, std::uint32_t deadline) {
    m_waiting = true;
    m_wait = type;
    m_waitObject = object;
    m_waitSeq = seq;
    m_deadline = deadline;
}

void Script::waitAnim(std::string_view name, int seq) {
    // 0x458880: only while the anim plays that sequence and has not finished it.
    Anim* a = m_engine.anims().find(name);
    if (a && a->sequence() == seq && !a->stopped())
        waitFor(Wait::Anim, a, seq);
    else
        log(std::format("Script error: can't find anim {}", name));
}

void Script::waitSound(std::string_view name) {
    // 0x458c20.
    const Sound* s = m_engine.sounds().findLoaded(name);
    if (s && !s->stoppedFlag())
        waitFor(Wait::Sound, s);
}

void Script::waitScript(std::string_view name) {
    // 0x458d50.
    Script* s = m_engine.scripts().find(name);
    if (s && !s->ended())
        waitFor(Wait::Script, s);
}

// --- built-ins ----------------------------------------------------------------------------

bool Script::builtin(ScriptExecutor& ex, std::int16_t opcode, std::span<const ScriptArg> a) {
    auto text = [&](std::size_t i) { return std::string(ex.text(a[i].sym())); };
    Node* node = m_node;
    switch (opcode) {
    case op::kPlayAnim:
    case op::kPlayAnimAt: {
        // 0x4587c0 / 0x458930.
        const std::string name = text(1);
        Anim* anim = m_engine.anims().getOrCreate(name);
        if (!anim) {
            log(std::format("Script error: can't create anim {}", name));
            break;
        }
        anim->hide(false);
        if (anim->motionPauseCount() != 0)
            anim->pause(false);
        if (opcode == op::kPlayAnimAt)
            anim->setPosition(static_cast<float>(a[3].truncated()), static_cast<float>(a[4].truncated()), anim->z());
        anim->setSequence(a[2].truncated(), true);
        if (a[0].f() != 0.0f)
            waitAnim(name, a[2].truncated());
        break;
    }
    case op::kWaitAnim:
        waitAnim(text(0), a[1].truncated());
        break;
    case op::kHideAnim:
        if (Anim* anim = m_engine.anims().find(text(0))) {
            anim->pause(true);
            anim->hide(true);
        }
        break;
    case op::kKillAnim:
        m_engine.anims().remove(text(0));
        break;
    case op::kCopyAnim: {
        Anim* anim = m_engine.anims().getOrCreate(text(0), text(1));
        if (!anim)
            log(std::format("Script error: can't copy anim {}", text(0)));
        else
            anim->setPosition(static_cast<float>(a[2].truncated()), static_cast<float>(a[3].truncated()),
                              static_cast<float>(a[4].truncated()));
        break;
    }
    case op::kMoveAnim:
        if (Anim* anim = m_engine.anims().find(text(0))) {
            float dz = a[3].f();
            if (dz == -1.0f)
                dz = anim->z(); // quirk: adds the depth to itself
            anim->move(a[1].f(), a[2].f(), dz);
        }
        break;
    case op::kPlaceAnim:
        if (Anim* anim = m_engine.anims().find(text(0))) {
            const float z = a[3].f() == -1.0f ? anim->z() : a[3].f();
            anim->setPosition(a[1].f(), a[2].f(), z);
        }
        break;
    case op::kPlaySound: {
        // 0x458bc0: PlaySound(sound, channel, queue, preempt, 1, 0, volume, -1, 0, 0).
        const std::string name = text(1);
        const bool ok = m_engine.sounds().playSound(name, static_cast<std::int16_t>(a[2].truncated()),
                                                    a[4].truncated() != 0, a[5].truncated() != 0, 1, false,
                                                    static_cast<std::int16_t>(a[3].truncated()), -1, 0, false);
        if (ok && a[0].f() != 0.0f)
            waitSound(name);
        break;
    }
    case op::kWaitSound:
        waitSound(text(0));
        break;
    case op::kStopSound:
        m_engine.sounds().stopSound(text(0));
        break;
    case op::kStopChannel:
        m_engine.sounds().stopChannelHead(a[0].truncated());
        break;
    case op::kStopAllSounds:
        m_engine.sounds().stopAll(false);
        break;
    case op::kRunScript:
        if (node) {
            node->runScript(text(1));
            if (a[0].f() != 0.0f)
                waitScript(text(1));
        }
        break;
    case op::kWaitScript:
        waitScript(text(0));
        break;
    case op::kStopScript:
        if (node)
            node->stopScript(text(0));
        break;
    case op::kStopAllScripts:
        if (node)
            node->stopAllScripts();
        break;
    case op::kSetKillWithNode:
        m_killWithNode = a[0].f() != 0.0f;
        break;
    case op::kNodeMessage:
        if (node)
            node->scriptMessage(*this, ex.text(a[0].sym()), m_counter);
        break;
    case op::kNodeNumber:
        if (node)
            node->scriptNumber(m_name, a[0].f());
        break;
    case op::kWait:
        // 0x458ec0: always waits, even for ms <= 0.
        waitFor(Wait::Timer, nullptr, 0, m_engine.ticks() + static_cast<std::uint32_t>(a[0].truncated()));
        break;
    case op::kGotoNode:
        // 0x458ef0: survives the switch and waits until the node is entered.
        m_waitName = text(0);
        m_engine.requestNode(m_waitName, 16, true);
        m_killWithNode = false;
        waitFor(Wait::Node);
        break;
    case op::kPauseNode:
        if (node) {
            const bool on = a[0].f() != 0.0f;
            node->pause(on);
            if (on) {
                m_engine.sounds().pauseAll();
                if (m_paused)
                    m_paused = false; // this script keeps running
            } else {
                m_engine.sounds().resumeAll();
            }
        }
        break;
    default:
        break; // unimplemented in the original: arguments popped, nothing done
    }
    return m_waiting;
}

// --- ScriptSystem ----------------------------------------------------------------------

ScriptSystem::~ScriptSystem() { deleteAll(); }

Script* ScriptSystem::find(std::string_view name) const {
    const auto it = m_scripts.find(str::lower(name));
    return it == m_scripts.end() ? nullptr : it->second.get();
}

bool ScriptSystem::load(Node&, std::string_view name) {
    log::debug("ScriptStarted - {}", name);
    if (find(name))
        return true;
    auto found = m_engine.resources().find(formats::chunk::kScript, name);
    if (!found) {
        log::warn("script {}: not found", name);
        return false;
    }
    auto program = formats::parseScript(m_engine.resources().read(found));
    if (!program) {
        log::warn("script {}: {}", name, program.error());
        return false;
    }
    ScriptExecutor exec(std::move(*program));
    m_scripts[str::lower(name)] = std::make_unique<Script>(m_engine, std::string(name), std::move(exec));
    return true;
}

bool ScriptSystem::start(Node& node, std::string_view name) {
    Script* s = find(name);
    if (!s)
        return false;
    node.scriptStarted(s->name());
    s->start(node);
    return true;
}

bool ScriptSystem::runScript(Node& node, std::string_view name) { return load(node, name) && start(node, name); }

bool ScriptSystem::stopScript(Node& node, std::string_view name) {
    Script* s = find(name);
    if (!s)
        return false;
    s->preempt(node);
    return true;
}

void ScriptSystem::stopAll(Node& node) {
    std::vector<Script*> all;
    for (auto& [k, s] : m_scripts)
        all.push_back(s.get());
    for (Script* s : all)
        if (std::ranges::any_of(m_scripts, [&](const auto& kv) { return kv.second.get() == s; }))
            s->preempt(node);
}

bool ScriptSystem::isRunning(std::string_view name) const {
    const Script* s = find(name);
    return s && !s->ended();
}

bool ScriptSystem::setVar(std::string_view script, std::string_view var, std::string_view value) {
    Script* s = find(script);
    if (!s)
        return false;
    const int id = s->executor().findSymbol(var);
    if (id < 0)
        return false;
    s->executor().setString(id, value);
    return true;
}

bool ScriptSystem::setVar(std::string_view script, std::string_view var, double value) {
    Script* s = find(script);
    if (!s)
        return false;
    const int id = s->executor().findSymbol(var);
    if (id < 0)
        return false;
    s->executor().setFloat(id, static_cast<float>(value));
    return true;
}

void ScriptSystem::deleteEnded() {
    std::erase_if(m_scripts, [](const auto& kv) { return kv.second->ended(); });
}

void ScriptSystem::deleteForNode(Node&) {
    std::erase_if(m_scripts, [](const auto& kv) { return kv.second->killWithNode(); });
}

void ScriptSystem::deleteAll() { m_scripts.clear(); }

int ScriptSystem::rand() { return m_engine.rand(); }

} // namespace brc::engine
