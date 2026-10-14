# Human Code script runtime (hcscript / exec / stack)

How the original engine loads, schedules and executes the compiled scripts described in
[`docs/formats/script.md`](../formats/script.md), what every built-in does in engine
terms, and how C++ node code talks to scripts. Addresses are in `Barbie Riding Club.exe`.
Class names are ours (the executable only gives the source files
`E:\Projects\Omt\Godiva\hcscript.cpp`, `exec.cpp`, `stack.cpp`); everything marked
*(inferred)* has not been observed directly.

## Overview

```
HCNode::RunScript("name")             C++ (node code, HCAn sequence actions)
  └─ HCScript  (0x70 bytes, hcscript.cpp)   one per loaded script, kept in a GLOBAL map name → HCScript*
       ├─ listener on the global broadcaster [0x80a43c]   (receives 0x64…0x6d messages)
       └─ Executor (0xac bytes, exec.cpp)                 bytecode interpreter
            ├─ value stack, 30 slots (stack.cpp)
            └─ symbol table: Symbol[nsym] (0x54 bytes each)
```

* Scripts are **cooperative**: a script runs synchronously until it executes a waiting
  built-in or reaches the end. There is no instruction budget or time slicing.
* A waiting script is woken by a broadcast message (timer, anim, sound, script, node) and
  continues on the next **node idle broadcast** (message 0x68, once per main-loop
  iteration).
* All script state is per `HCScript` instance; variables are not shared between scripts.
  C++ reads nothing back; it writes variables (by name) and receives callbacks.

## Executor (exec.cpp)

### Layout (0xac bytes, ctor 0x445820)

| Offset | Type | Meaning |
|---|---|---|
| +0x00 | u8* | chunk base (start of the SCR chunk data, owned by the HCScript) |
| +0x04 | u8* | pc |
| +0x08 | u8* | end of code = base + symoff |
| +0x0c | Symbol** | symbol table, indexed by symbol id |
| +0x10 | i32 | symbol count |
| +0x14 | u8 | running (1 after a successful load, 0 at end of code / error) |
| +0x18 | u32[30] | value stack slots |
| +0x90 | i32 | stack pointer (number of used slots) |
| +0x94 | HCScript* | owner (target of all built-in calls) |
| +0x98 | i16 | yield flag |

Constructor (owner, buffer, size): base = pc = buffer, running = 0, yield = 1, owner set.
Reads the version (must be 5, else logs and stays not running), reads `symoff`, sets
end = base + symoff, reads `nsym`, allocates the table, then for each record creates a
Symbol, stores it at `table[id]` and advances by the record size; a bad type logs
`Script executor error: error loading symbol table` and stops (not running). On success
running = 1 and pc = base + 8. `size` is not used.

### Symbols (0x54 bytes; ctor 0x45ba60, load 0x45bb50)

| Offset | Meaning |
|---|---|
| +0x00, +0x04 | always NULL; the destructor 0x45ba80 frees them recursively *(unused tree links, inferred)* |
| +0x08 | id |
| +0x0c | char[32] name / literal text |
| +0x2c | value: float, or char[32] string (zeroed at load: 0.0 / "") |
| +0x4c | type: 0 float, 1 string constant, 2 string variable |

* `SetFloat` 0x45bb30: value = f, **type = 0**.
* `SetString` 0x45bac0: copies at most 31 characters + NUL into the value, **type = 2**.
* "Text of a symbol" (used by every string operation and every built-in taking a name) is
  the name field for type 1 and the value field for types 0 and 2.

### Value stack (stack.cpp, embedded at executor+0x18)

30 four-byte slots. Push (0x45b9c0 float, 0x45b9e0 16-bit) checks `sp == 30` first
(0x45b940): on overflow it logs `script stack overflow!` and **drops** the value. Pop
(0x45ba10 / 0x45ba30) checks `sp == 0` (0x45b980): on underflow it logs `script stack
underflow!` and leaves the destination unchanged (an uninitialised local in the opcode
handler). A 16-bit push writes only the low half of the slot. No script on the disc goes
deeper than 6; the only underflow on the disc is the `PagerDelay` jump-to-header quirk,
whose popped value is discarded anyway.

### Run loop (0x445a10)

```
Run():
  if not running: return
  yield = 0
  while pc < end and yield == 0:
      op = read i16 at pc; pc += 2
      dispatch(op)        # 1..11 inline, 0xd–0x23 → 0x445c60, 0x24–0x2a → 0x446290,
                          # 0x2b–0x3b → 0x446590, 0x3c–0x47 → 0x446f20,
                          # 0x48–0x53 → 0x446c50, ≥ 0x54 → 0x447280
  if pc >= end: running = 0
```

* Unknown opcode (any group): log `Bad error: unknown opcode %ld.`, pc = end.
* JZ / JMP: `pc = base + target` where base is the chunk start (not the code start).
* Only waiting built-ins set `yield`. After a yield, pc already points past the built-in;
  the next `Run` continues there.
* A wait as the very last instruction makes `running = 0` immediately, but the script is
  not treated as ended until the wait is over (see `Execute`).

### Arithmetic and string details

* Floats are IEEE single; comparisons and logic produce exactly 1.0 or 0.0; JZ tests
  `== 0.0` (x87 compare, so −0.0 counts as zero).
* Float→int conversions (MOD, RAND, every integer built-in argument) use `_ftol`
  (truncation toward zero).
* `RAND n` = `rand() % trunc(n) + 1` with the MSVC CRT `rand` (0x7dc460: seed·214013 +
  2531011, bits 16–30). The seed is whatever the rest of the game left it at
  *(not traced)*.
* Strings are at most 31 characters. `SCONCAT` copies the left text (≤ 31) and appends the
  right text while space remains. `NUM2STR` formats `"%-*f"` with width 31 into a 32-byte
  buffer, then walks back from index 30 removing `'0'` and `' '`, then removes one
  trailing `'.'`.
* `STRCMP` is an inline unsigned byte compare returning −1/0/+1; `STRICMP` is the CRT
  `_strcmpi` (0x7e99e0) result converted to float.
* Lookups by name from C++ (`FindSymbol` 0x4477d0) scan the table linearly with a
  **case-sensitive** `strcmp` on the name field.

## HCScript (hcscript.cpp)

### Layout (0x70 bytes, ctor 0x458fb0, dtor 0x459350)

| Offset | Type | Meaning |
|---|---|---|
| +0x00 | vtable 0x7f4798 | [0] ListenToMessage 0x4586b0, [1] deleting dtor 0x459330, [2] 0x45bd80 |
| +0x04, +0x08 | | OMT listener base (0x45bbd0): list of broadcasters, count |
| +0x0c | i16 | listener "abort broadcast" flag (cleared by the broadcaster, never set here) |
| +0x10 | i16 | started |
| +0x14 | i32 | resume counter (starts at 1, +1 after every completed run slice and every wake) |
| +0x1c | i32 | counter value passed to the current `Execute`; forwarded to `nodeMessage` handlers (no handler uses it *(inferred)*) |
| +0x20 | HCNode* | node passed to the current `Execute` (target of node built-ins) |
| +0x24 | HCApplication* | from node+0x90 (sound and node-switch built-ins) |
| +0x28 | i32 | chunk id |
| +0x2c, +0x30 | char*, i16 | script name as passed by C++ (map key, callback argument), its length |
| +0x34 | i16 | loaded OK |
| +0x36 | i16 | set by opcode 0x5c, never read |
| +0x38 | Executor* | |
| +0x3c | u8* | copy of the SCR chunk |
| +0x40, +0x48 | | two empty lists, never used *(inferred)* |
| +0x50 | i16 | kill-with-node (default 1; opcode 0x5b; cleared by `gotoNode`) |
| +0x52 | i16 | ended |
| +0x54 | i16 | waiting |
| +0x58 | i32 | wait type (0x65, 0x69, 0x6a, 0x6c, 0x6d) |
| +0x5c, +0x60 | | wait parameters (below) |
| +0x68 | i16 | paused |
| +0x6c | u32 | `GetTickCount` when paused |

Constructor (name, node): app = node+0x90; looks the name up as `'SCR '` in the open OMT
files (0x44dca0, case-insensitive), copies the name, reads the whole chunk into a new
buffer, creates the executor; if the executor is running: started = 0, counter = 1,
loaded = 1 and the script registers as a listener on the global broadcaster
(`[0x80a43c]` vt+8). A missing chunk or bad bytecode leaves loaded = 0.

### Message handler 0x4586b0 (msg, data)

`data` points to three dwords supplied by the broadcaster of the message.

1. msg 0x66 → `Pause` 0x459630 (if not paused: paused = 1, remember `GetTickCount`).
   msg 0x67 → `Unpause` 0x459650 (if paused: if waiting on a timer, deadline += time spent
   paused; paused = 0).
2. If waiting and not paused, test the wake condition:

   | Wait type | Set by | Wakes when |
   |---|---|---|
   | 0x65 timer | `wait` | on **any** message: `GetTickCount() >= deadline` (+0x5c, unsigned) |
   | 0x69 node | `gotoNode` | msg 0x69 and `_strcmpi(data[0], +0x5c) == 0` (node name) |
   | 0x6a sound | `waitSound`, `playSound(wait)` | msg 0x6a and `data[1] == +0x60` (sound's name pointer) |
   | 0x6c anim | `waitAnim`, `playAnim(wait)` | msg 0x6c and `data[0] == +0x5c` (anim's name pointer) and `data[1] == +0x60` (sequence) |
   | 0x6d script | `waitScript`, `runScript(wait)` | msg 0x6d and `data[0] == +0x5c` (other script's name pointer) |
   | 100, 0x6b | never set | (handler compares `data[0]`/`data[1]` respectively — dead code) |

   Pointer comparisons are exact: the waiting side stores the very pointer the
   broadcaster later sends. Wake = waiting = 0, counter + 1.
3. Then, if not waiting, not ended, started, not paused and msg == 0x68:
   `Resume(data[0])` (data[0] is the node that broadcast the idle message).

So a timer that expires is noticed at the next broadcast of any kind; if that broadcast is
the idle message 0x68 the script continues in the same call, otherwise on the next 0x68.

### Start / Resume / Execute / Preempt

* `Execute(counter, node)` 0x458640: sets +0x20 = node (and the unused global
  `[0x80a484]` = this). If not ended, not waiting, not paused and the executor is running:
  +0x1c = counter, waiting = 0, `Executor::Run()`. Afterwards, if the executor is not
  running and the script is not waiting: ended = 1.
* `Start(node)` 0x459510, used once when C++ starts the script: if not ended and not
  started: started = 1, then loop {if waiting: stop; Execute; if not waiting: counter + 1}
  until ended. If ended: `node->ScriptEnded(name)` (vt+0x90).
* `Resume(node)` 0x459580, from the idle message: same, but only if started, and the loop
  also stops when paused.
* `Preempt(node)` 0x4595f0 (StopScript): if not ended: ended = 1, waiting = 0,
  `node->ScriptPreempted(name)` (vt+0x8c). The executor is not touched: a script that stops
  itself keeps running until its next wait, and then also gets `ScriptEnded`.
* Ended scripts stay in the map until the next node idle (below). *(Inferred
  consequence, matches the code:)* `RunScript` of a name whose previous instance has ended
  but not yet been deleted finds the old instance and `Start` does nothing, so a script
  cannot be restarted from its own `ScriptEnded` callback in the same frame.
* `Start` has no pause check: starting a script while it is paused would loop forever
  *(inferred; cannot happen because a new script is never paused)*.

## Built-ins

All built-in opcodes call a method of the owning `HCScript`. Argument order below is the
push order (see the format document); `name` arguments are symbol texts. "Waits" means
the built-in sets waiting = 1, the wait type/parameters and the executor's yield flag.
Unlisted opcodes (0x2d–0x30, 0x34, 0x36, 0x37, 0x3c, 0x3d, 0x43–0x45, 0x48–0x55, 0x5a,
0x62–0x65) pop their arguments and do nothing.

### Animations (HC2DAnim objects, global name map `[0x80a410]`)

`CreateAnim(name, alias, 0, 0)` 0x454ce0 returns the anim called `name` from the global
map, attaching it to the current node (`app+0xec`) if it is not attached, or else creates
a new `HC2DAnim` (0x447a00) from the `HCAn` resource `name`, renames it to `alias` when
given, attaches it and adds it to the map. `FindAnim(name)` 0x455020 is a map lookup
(case-insensitive). Anim virtuals used: +0x04 `place(x, y, z)` 0x467540 (absolute, floats),
+0x08 `move(dx, dy, dz)` 0x467600 (relative), +0x30 `hide(bool)` 0x467f70 (counted),
+0x38 `pause(bool)` 0x447b50 (counted, count at +0x5c); the sequencer sub-object at +0xc4,
vt+4 = `StartSequence(seq, 1)` 0x449540; +0xc = z, +0xd8 = current sequence,
+0x100 = name, +0x148 = current sequence finished.

| Op | Built-in | Method | Behaviour |
|---|---|---|---|
| 0x2b | playAnim(wait, anim, seq) | 0x4587c0 | `a = CreateAnim(anim)`; NULL → log `Script error: can't create anim %s`. Else `hide(false)`; if the pause count is non-zero `pause(false)`; `StartSequence(seq, 1)`; if wait → waitAnim(anim, seq). |
| 0x2c | playAnimAt(wait, anim, seq, x, y) | 0x458930 | as playAnim, plus `place((float)x, (float)y, current z)` before starting the sequence. |
| 0x31 | waitAnim(anim, seq, –, –) | 0x458880 | `a = FindAnim(anim)`; if `a` exists, `a.currentSeq == seq` and the sequence has not finished: wait for 0x6c with (+0x5c = `a.name` pointer, +0x60 = seq). Otherwise log `Script error: can't find anim %s` (also when the anim plays another sequence or has finished) and continue. Arguments 3 and 4 are ignored. |
| 0x32 | hideAnim(anim) | 0x458a10 | if found: `pause(true)`, `hide(true)`. |
| 0x33 | killAnim(anim) | 0x458a50 → 0x4550b0 | if found: `hide(true)`, remove from the map, queue for deletion at the next node idle (0x4552d0). |
| 0x35 | copyAnim(resource, newName, x, y, z) | 0x458a80 | `CreateAnim(resource, newName)`; NULL → log `Script error: can't copy anim %s` (resource); else `place((float)x, (float)y, (float)z)`. |
| 0x38 | moveAnim(anim, dx, dy, dz) | 0x458b60 | if found: if `dz == −1.0` then dz = current z; `move(dx, dy, dz)`. **Quirk:** −1 therefore *adds* z to itself (doubles a non-zero z). All 7 uses on the disc (carousel, tackroom) pass −1 or 0. |
| 0x39 | placeAnim(anim, x, y, z) | 0x458b10 | if found: z == −1.0 keeps the current z; `place(x, y, z)`. |

### Sounds (HCApplication sound manager; see the BarbieRecomp notes on 0x44c3c0)

| Op | Built-in | Method | Behaviour |
|---|---|---|---|
| 0x3e | playSound(wait, sound, channel, volume, queue, preempt) | 0x458bc0 | `app->PlaySound(sound, channel, queue, preempt, loops = 1, keep = 0, volume, pan = −1, startMs = 0, stream = 0)` (0x44c3c0). If it returns non-zero and wait: waitSound(sound). Scripts on the disc always pass channel −1 (first free), volume −1 (default), queue 0, preempt 0. |
| 0x3f | waitSound(sound) | 0x458c20 | `s = app->FindSound(sound)` (0x44c930, loaded-sound list, case-insensitive); if found and its flags lack 2 (stopped): wait for 0x6a with +0x60 = `s`'s name pointer. |
| 0x40 | stopSound(sound) | 0x458c80 → 0x44c700 | FindSound → `Stop` (sound vt+0x20). |
| 0x41 | stopChannel(ch) | 0x458cb0 | sound at the head of channel `(short)ch` (0x44c900, 0..15) → `Stop`. |
| 0x42 | stopAllSounds() | 0x458cd0 → 0x44c780(0) | `Stop` the head sound of all 16 channels. |

### Scripts, node, time

| Op | Built-in | Method | Behaviour |
|---|---|---|---|
| 0x56 | runScript(wait, script) | 0x458d10 | `node->RunScript(script)` (0x455890, synchronous: the new script runs until its first wait); if wait → waitScript. |
| 0x57 | waitScript(script) | 0x458d50 | if the script is in the global map and not ended: wait for 0x6d with +0x5c = its name pointer. |
| 0x58 | stopScript(script) | 0x458dd0 | `node->StopScript(script)` (0x455c10). |
| 0x59 | stopAllScripts() | 0x458e00 | `node->StopAllScripts()` (0x455bb0) — every script in the map, the caller included. |
| 0x5b | setKillWithNode(flag) | 0x458e30 | +0x50 = (flag != 0). |
| 0x5c | setFlag36(flag) | 0x458e10 | +0x36 = (flag != 0); nothing reads it. |
| 0x5d | nodeMessage(msg) | 0x458e50 | `node->ScriptMessage(scriptName, text(msg), counter)` (node vt+0x98). |
| 0x5e | nodeNumber(n) | 0x458e90 | `node->vt+0x94(scriptName, n)`; the default is empty and no game node overrides it. |
| 0x5f | wait(ms) | 0x458ec0 | deadline = `GetTickCount()` + ms (int); wait type 0x65. Always waits, even for ms ≤ 0 (then the next message of any kind wakes it). |
| 0x60 | gotoNode(node) | 0x458ef0 | `app->RequestNode(node, 16, 1)` (app vt+0x1c = 0x44de80: stores the pending node name; the switch happens later in the application idle); kill-with-node = 0 so the script survives the switch; wait for 0x69 with +0x5c = the name text. |
| 0x61 | pauseNode(flag) | 0x458f50 | `node->Pause(flag)` (vt+0x2c = 0x454bc0, broadcasts 0x66 when the node's pause count becomes 1 and 0x67 when it returns to 0, pausing every script); then if flag: pause all 16 sound channels (0x44c850, sound vt+0x14) and un-pause *this* script (0x459650) so it keeps running; else resume all channels (0x44c880, vt+0x18). |

## Messages (global broadcaster)

The broadcaster at `[0x80a43c]` (vtable 0x7f4c38; vt+4 `Broadcast(msg, data)` 0x45c580,
vt+8 `AddListener` 0x45c510) calls listeners in registration order, fetching the next
link before each call (a listener added during a broadcast is reached in the same
broadcast). All senders are `HCNode` virtuals (vtable 0x7f45f8) that build a 3-dword
block on the stack:

| Msg | Sender | data | Meaning |
|---|---|---|---|
| 0x64 | vt+0x54 0x454c40, when event[0] == 2 | [0] = event[2], [1] = event | input event *(inferred)*; no script waits for it |
| 0x66 / 0x67 | `HCNode::Pause` 0x454bc0 | NULL | node paused / resumed |
| 0x68 | node idle task 0x456f30 | [0] = node | once per main-loop iteration |
| 0x69 | vt+0x48 0x454c10, called by the application right after making a node current (0x44b9c7) | [0] = node name (+0x88) | node entered |
| 0x6a | vt+0x80 0x457010, called by a sound's `Stop` (0x44f7fa) with its name | [1] = sound name pointer | sound finished |
| 0x6b | vt+0x84 0x456fe0 | [1] = argument | no script waits for it |
| 0x6c | vt+0x78 0x454ca0, called by `HC2DAnim` when a sequence completes (0x449660, 0x449540) | [0] = anim name pointer, [1] = sequence | anim sequence done |
| 0x6d | vt+0x8c / vt+0x90 0x457040 | [0] = script name pointer | script preempted / ended |

Node subclasses that override vt+0x78, +0x80, +0x8c or +0x90 call the base version at the
end, so the messages always go out (e.g. 0x41c460, 0x41dc80, 0x43b000).

## Scheduling per frame

The game has no frame limiter: the main loop (0x47a970) calls every idle task once per
iteration (BarbieRecomp `api_usage.md` §2.4). The current node is an idle task (secondary
vtable at node+0xc, 0x7f45f0, slot 1 = 0x456f30). Its idle does, in this order:

1. delete the anims queued by `killAnim`/`DeleteAnim` (0x4552d0);
2. walk the global script map and delete every script whose *ended* flag is set (deleting
   unregisters it from the broadcaster);
3. broadcast 0x68 with data[0] = this node — every live script checks its timer and, if
   runnable, resumes until its next wait or its end;
4. the base idle (0x45cf50).

Timing therefore has two clocks: `wait(ms)` uses `GetTickCount` (milliseconds, coarse on
Windows 9x), while continuation happens at the next frame; `wait(1)` in a polling loop means
"check again next frame".

## Lifetime and the C++ API (HCNode.cpp)

The script map is a **global** `std::map<string, HCScript*>` at 0x80a420 (case-insensitive
keys = the names C++ passed in). Methods are on `HCNode`; only the callbacks use the node.

| Address | Method | Behaviour |
|---|---|---|
| 0x4558b0 | `LoadScript(name)` | logs `ScriptStarted - %s`; returns true if the name is already in the map; else `new HCScript(name, this)` and inserts it if loaded, deletes it and returns false otherwise |
| 0x455b40 | `StartScript(name)` | if in the map: `this->ScriptStarted(name)` (vt+0x88), `script->Start(this)`; true |
| 0x455890 | `RunScript(name)` | `LoadScript` then `StartScript` |
| 0x455c10 | `StopScript(name)` | if in the map: `script->Preempt(this)` |
| 0x455bb0 | `StopAllScripts()` | `Preempt(this)` for every script in the map |
| 0x455840 | `IsScriptRunning(name)` | in the map and not ended |
| 0x455cb0 | `SetScriptVar(script, var, const char*)` | `FindSymbol(var)` (case-sensitive) → `SetString` (type becomes 2); false if script or variable missing |
| 0x455d60 | `SetScriptVar(script, var, double)` | same with `SetFloat((float)value)` (type becomes 0) |
| 0x455730 | delete all scripts | called from the application destructor 0x44a650 |

Deletion: ended scripts at the next node idle; the `HCNode` destructor (0x454880) deletes
every script whose kill-with-node flag is set **without** any callback, whatever its
state; scripts with the flag cleared (after `gotoNode`, or `setKillWithNode(0)`) survive the
node switch and are resumed by the next node's idle broadcasts (their +0x20 becomes the new
node).

`HC2DAnim` sequence-end actions can also start or stop scripts: in 0x4497b0 flag 0x80 of
the sequence definition calls `RunScript(def+0x40)` and flag 0x100 calls
`StopScript(def+0x44)` (the `HCAn` format belongs to the animation documentation).

### Node callbacks (HCNode vtable slots)

| Slot | Default | Called | Game overrides |
|---|---|---|---|
| +0x88 `ScriptStarted(name)` | 0x49d710 (empty) | `StartScript`, before `Start` | TrailNode 0x43aa40 (stops `HorseAmb` when `mount` starts, …) |
| +0x8c `ScriptPreempted(name)` | 0x457040 → msg 0x6d | `Preempt` | TrailNode 0x43b000 (logs `TNScrPreempted-` with `OutputDebugStringA`, then chains other scripts) |
| +0x90 `ScriptEnded(name)` | 0x457040 → msg 0x6d | end of `Start`/`Resume` | game-node base 0x41dc80 (after `DelayVoice` ends, plays the voice clip stored at node+0x180); TrailNode 0x43aab0 (logs `TNScriptEnded -`, chains e.g. `LookLeft` → `Ambient`, `mount` → `RunOff`/`Ambient`, `HorseAmb` → `TMoment2`); applesort 0x403ca0, race 0x4263e0, credits 0x410f70, yard 0x43e5f0 |
| +0x94 `ScriptNumber(name, n)` | 0x458cf0 (empty) | `nodeNumber` | none |
| +0x98 `ScriptMessage(name, msg, counter)` | 0x458d00 (empty) | `nodeMessage` | see below |

### The `nodeMessage` handshake

Scripts get their parameters by sending a message and letting the node answer
synchronously with `SetScriptVar` before the next instruction runs. Typical start:
`horse = ""; fancy = 0; nodeMessage("initRace")` and then `playAnim(…, horse, …)`.

| Handler | Node(s) | Messages handled (variables set) |
|---|---|---|
| 0x41dcd0 | base of most Godiva game nodes (bulletinboard, grooming, signin, stable, vinegame, …); the leapfrog and tackroom handlers fall back to it | `initDelay` (delay = node+0x190), `initDelayVoice` (delay = node+0x184), `voice <wave>` (plays WAVE `<wave>` via 0x41d270: channel 13 music ducked to volume 0x18, clip on channel 14 at volume 0x70), `SayHorseName` (only the first 6 characters are compared; node vt+0xc8) |
| 0x43a080 | TrailNode family (vtable 0x7f03a0 and its copies) | `initAmbient`, `initHLookLeft`, `initHMountRun`, `initHTurnLeft/Right`, `initHorseAmb`, `initTMoment2`, `initM5Payoff`, `initPagerDelay`, `initWalk`, `initWalkAmb`, `preload_getnext` (sets `animName` to the next anim to preload or `""`), `PagerOKToGo`, … (variables hAmbient, hLookLeft, hMountRun, hTurnLeft/Right, generalHorseAmb, subtleHorseAmb, hTMoment2, hEatApple, fancy, delay, talking, busy, cycles, direction, lastDirection, animName) |
| 0x426140 | race | `initRace` (horse, fancy, startSound, startSeq), `initCRace` (competitor), jumpseq, `startUp` |
| 0x403a70 | applesort | `initFlippers` (flipper1–4 strings, seq1–4, delay) |
| 0x410420 | carousel | `initWalk` (cycles, direction, lastDirection), `hideWalkAmb` |
| 0x42f280 | tackroom | `toolbartour`, `ScrollMiddle`, `ScrollRight`, `tackTourEnded`, voice ids |
| 0x420cf0 | leapfrog | `duck`, `babyduck`, `frog2`, `initDelay` (delay = node+0x1c4) |
| 0x43e530 | yard | sets hAmbient, hMountRun, fancy |
| 0x419e10 | HCLogoNode | sets `faded` for the `music` script |

The credits node sets `done`/`nextPage` of `credits` from two other virtuals (0x410ec0,
0x410f20; input handlers *(inferred)*) while the script polls them every 100 ms.

## Quirks to reproduce

* `StopScript("LookLeft.scr")`, `"ExcitedLook.scr"`, `"M5Payoff.scr"` (TrailNode 0x442af0,
  0x442d30) never match anything: the map keys are `LookLeft`, `ExcitedLook`, `M5Payoff`,
  so these calls do nothing.
* `trails.omt` `PagerDelay` jumps to chunk offset 0 and ends early when `delay > 250` (see
  the format document).
* `moveAnim` with dz = −1 doubles the anim's z.
* `waitAnim` logs an error (and does not wait) when the anim is playing a different
  sequence or has finished it.
* A script that stops itself gets both `ScriptPreempted` and `ScriptEnded`.
* Re-running a script from its own `ScriptEnded` callback does nothing until it has been
  deleted at the next node idle.

## Implementation notes for OpenBRC

* Keep the bytecode as the source of truth: interpret it (all opcodes in the format table,
  including the no-op built-ins and their pop counts) rather than translating scripts.
* Keep the global name→script map, the "ended, delete on next idle" rule, the
  kill-with-node flag and the exact wake rules (pointer identity can be replaced by object
  identity: anim instance + sequence, sound instance, script instance, node name).
* Continue woken scripts only from the per-frame idle broadcast; run new scripts
  synchronously inside `RunScript`.
* `local/re/scr_disasm.py` produces listings and pseudo-source for every script on the disc
  (`local/extract/scripts/`), handy for tests: e.g. `common` `Delay` must send `initDelay`
  and then wait exactly the `delay` the node set.
