# Sound, music and voice (HCApplication.cpp on Miles 4.0g)

All game audio goes through the Human Code sound manager in `E:\Projects\Omt\Godiva\HCApplication.cpp`,
which wraps the Miles Sound System 4.0g "quick" API (memory sounds) and stream API (music). Smacker
movie audio also plays through Miles (see `movies.md`). DirectSound and `PlaySoundA` are linked but
never used for game sounds (BarbieRecomp `docs/api_usage.md` §4).

The Miles calls themselves (exact arguments, `HAUDIO` layout, status codes) are documented in
BarbieRecomp `docs/api_usage.md` §5. This document describes the engine logic above them: channels,
queues, reference counts, completion events, volumes, music selection, voice ducking, loading and
sample-rate conversion. Addresses are virtual addresses in `Barbie Riding Club.exe`.

Validation script: `local/re/snd_waves.py` (every WAVE chunk on the disc, 991/991 parsed).

---

## 1. Startup, global state

* `HCApplication::Init` (`0x44aa30`) reads registry pref `"Sound"` (default 1). If it is 1 it sets
  `AIL_set_preference(DIG_MIXER_CHANNELS, 16)` and `AIL_quick_startup(digital, no MIDI, 22050 Hz,
  16-bit, stereo)`, keeps the `HDIGDRIVER` at `app+0xD0` and calls `SmackSoundUseMSS(app+0xD0)`.
* **`app+0xD0 == 0` disables all sound**: `GetSound` returns NULL, so `PlaySound` returns 0, nothing
  is loaded and no completion events are ever sent. Game logic that waits for a sound-done event then
  never advances *(inferred: e.g. the dialogue chains in §6.2)*.
* Shutdown (`0x44a650`): `StopAll(purge=1)` (`0x44c780(1)`), then `AIL_quick_shutdown`.

HCApplication fields used by the sound manager (app object at `[0x80a384]`):

| Offset | Type | Meaning |
|---|---|---|
| +0x48 | list* | **Loaded-sound list** (circular doubly linked; node = {next, prev, Sound*}) |
| +0x4C | int | number of loaded sounds |
| +0x50 + ch·8 | list* | **channel `ch` queue** (circular list; node = {next, prev, loaded-list node*}) for ch = 0..15 |
| +0x54 + ch·8 | int | number of entries in channel `ch`'s queue (0 = channel free) |
| +0xD0 | HDIGDRIVER | Miles digital driver; 0 = no sound |
| +0xEC | Node* | current node = receiver of `SoundDone` / `MovieDone` (set by `0x44df10`, app vtable slot +0x24) |
| +0xFC | u16 | fade step divisor used by the master-volume fade (§9), ≤ 16 |
| +0x102 | u16 | "fade sound too" flag for node transitions (§9) |

Node fields (Godiva node base, constructor `0x41b690`):

| Offset | Meaning |
|---|---|
| +0x178 | char* name of the node's **ambient loop** (or 0) |
| +0x17C | u16 voice active (set by PlayVoice/QueueVoice, cleared when channel 14 drains) |
| +0x180 / +0x184 | pending DelayVoice: voice name / delay in ms |
| +0x188 / +0x18C | mouth-flap animation shown / its HC2DAnim* |
| +0x190 | Delay script length in ms |
| +0x1B8 | Godiva application* (its +0x344 is the **music off** flag) |

---

## 2. Sound objects

One object per *sound name*: `GetSound` returns the already-loaded object when the name (case
insensitive, `_strcmpi`) is in the loaded list, so a name can never play twice at once and a second
`PlaySound` of a playing name restarts it (§4.2).

Two classes share a layout; the stream class extends it:

| Off | Type | Field |
|---|---|---|
| +0x00 | vtable | `0x7f4288` memory sound (size 0x30, ctor `0x44f3e0`) / `0x7f42b8` stream (size 0x38, ctor `0x44f910`) |
| +0x04 | u16 | **use count** (reference count, see below) |
| +0x06 | s16 | volume for the next Play, −1 = don't touch |
| +0x08 | s16 | pan for the next Play, −1 = default |
| +0x0A | s16 | start offset in ms for the next Play (0 = from the start; every caller passes 0) |
| +0x0C / +0x10 | char* / u16 | name (OMT string: pointer, length) |
| +0x14 | node* | this object's node in the loaded list |
| +0x18 | void* | memory sounds: heap copy of the RIFF image |
| +0x1C | HAUDIO | memory sounds: Miles handle (game code also uses it directly, §5.3) |
| +0x20 | int | position in ms saved by Pause |
| +0x24 | int | loop count for the next Play (1 = once, **0 = forever**, n = n times); Pause overwrites it with the remaining count |
| +0x28 | u32 | flags: 1 playing, 2 stopped, 4 paused, 8 stop already notified |
| +0x2C | int | unused (0) |
| +0x30 | HCFileStream* | stream only: private file handle on the .omt |
| +0x34 | HSTREAM | stream only |

Virtual methods (slot: memory / stream):

| Slot | Method | Behaviour |
|---|---|---|
| +0x00 | delete | memory `0x44f480`: unlink from the loaded list, `AIL_quick_unload`, free buffer and name. Stream `0x44f950`: `AIL_close_stream`, delete the file stream, then as memory. |
| +0x04 | Update `0x44f6a0` | if playing (1) and not stopped (2) and `IsDone()` and not paused → `Stop()` |
| +0x08 | IsPlaying `0x44f430` | `(flags&1) && !IsDone() && !(flags&4)` |
| +0x0C | GetVolume | memory `0x44f540`: `AIL_sample_volume(HAUDIO->HSAMPLE)` (0xFFFF if not a digital sample). Stream `0x44f9e0`: `AIL_stream_volume`, −1 without a stream. |
| +0x10 | SetVolume | memory `0x44f590`: `AIL_set_sample_volume(HAUDIO->HSAMPLE, v)` (only effective while playing — Miles clears the HSAMPLE on halt). Stream `0x44fa00`: `AIL_set_stream_volume`. |
| +0x14 | Pause | memory `0x44f5c0`: save ms position and remaining loop count (a negative count is logged and replaced by 1), flag 4, `AIL_quick_halt`. Stream `0x44fa20`: save position and loop count, flag 4, `AIL_pause_stream(1)`. |
| +0x18 | Resume | only if flag 4: clear it; memory `0x44f670`: `AIL_quick_set_ms_position(saved)` + `AIL_quick_play(savedLoops)`; stream `0x44fa60`: set position, `AIL_pause_stream(0)`. |
| +0x1C | Play | memory `0x44f700`: fails if no buffer. If volume ≠ −1: `AIL_quick_set_volume(h, vol, pan == −1 ? 63 : pan)` (volume −1 leaves volume *and* pan alone). If start offset ≠ 0: set ms position. `AIL_quick_play(h, loops)`; on failure log `"Error starting sound %s: AIL_quick_play returned %ld."` and return 0. Stream `0x44fa90`: volume if ≠ −1, pan if ≠ −1, position if ≠ 0, loop count only if ≠ 1, `AIL_start_stream`. On success: use count +1, flags = (flags & ~(2\|8)) \| 1. |
| +0x20 | Stop `0x44f7b0` | see §2.1. Stream `0x44fb20` first does `AIL_pause_stream(1)`. |
| +0x24 | IsDone | memory `0x44f460`: `AIL_quick_status == QSTAT_DONE (1)`. Stream `0x44f930`: `AIL_stream_status == SMP_DONE (2)`. |
| +0x28 | Load | memory `0x44f840`, stream `0x44fb40` (§7) |

### 2.1 Stop and the completion event (`0x44f7b0`)

1. If flag 8 is not yet set: set it, then call the current node's **`SoundDone(name, completed)`**
   (`app+0xEC`, node vtable +0x80) with `completed = (flags & 1) && IsDone()` — 1 when the sound ran
   to its end, 0 when it was stopped early or never started. If the sound was playing, use count −1.
2. `AIL_quick_halt` (memory sounds with a handle).
3. flags = (flags & ~1) | 2.

`Stop` on an already-notified sound only halts it again. Flags 2 and 8 are cleared by the next
successful `Play`; flags 1 and 2 are also cleared when the service loop removes the sound from its
channel (`0x44f830`).

### 2.2 Use count and lifetime

| Event | Count |
|---|---|
| `GetSound(name, keep=1, …)` (found or new) | +1 |
| queued on a channel by `PlaySound` | +1 |
| service loop starts a queued sound | −1, then `Play` success +1 |
| `Stop` of a playing sound | −1 |
| `Retain(name, add)` `0x44c8b0` / `0x44c8d0` | +1 if add ≠ 0, else −1 |

At the end of every `Service(1)` pass, **every loaded sound whose count is 0 is deleted**
(unloaded). So a fire-and-forget sound is unloaded on the first service pass after it finishes, and a
sound played with `keep=1` (ambient loops, preloaded sounds) stays loaded until someone calls
`Retain(name, 0)`. *(Inferred quirk: a queued sound that is stopped before it ever started keeps its
+1 from the queue forever and is only freed by `StopAll(purge)`.)*

---

## 3. Channels

* **16 channels, 0..15**, each a FIFO queue of sounds. The head of the queue is the sound that plays;
  the others wait. A channel is *free* when its queue is empty.
* The game reserves two by convention (no code enforces it):

| Channel | Use |
|---|---|
| 13 (0xD) | **music** (`PlayMusic` `0x4133c0`, preempt) and the carousel "MusicBox" |
| 14 (0xE) | **voice** (Barbie's dialogue: `PlayVoice`/`QueueVoice`, §6) |
| 15 | overflow target when no channel is free and the caller asked to queue or preempt |
| −1 | "first free channel": the lowest-numbered empty queue in 0..15 — this **can** pick 13 or 14 when music/voice are idle *(observed in `0x44c3c0`; consequence inferred: such an effect is then cut by the next music preempt, or delays a queued voice line)* |

### 3.1 Service loop `Service(full)` (`0x44c990`)

Called with `full=1` once per main-loop pass from `HCApplication::Idle` (`0x44b4b0`, after a pending
node switch) and from `PlaySound` (with 0 at the end, with 1 when restarting a sound). For each
channel 0..15 with a non-empty queue, looking only at the **head**:

1. if the head's flags have neither 1 nor 2 (queued, never started): count −1 and `Play()`;
2. if `full`: `Update()` (detects the end and calls `Stop`, which sends `SoundDone`); then, if the
   head now has flag 2 (stopped): clear its flags 1|2 and remove it from the queue.

Then, if `full`: delete every loaded sound whose use count is 0.

Consequences: at most one queue entry is removed per channel per pass, and the next queued sound
starts on the following pass (one main-loop pass of silence between queued sounds). `IsDone` is
polled — no Miles callbacks are used.

### 3.2 Channel helpers

| Address | Name (ours) | Behaviour |
|---|---|---|
| `0x44c900` | ChannelSound(ch) | head sound of channel ch (0..15), else NULL |
| `0x44ca50` | ChannelBusy(ch) | 1 if the queue is non-empty and (head IsPlaying, or head not started, or more than one entry and the head is stopped or done); else 0 |
| `0x44c740` | StopChannel(ch) | `Stop()` every sound in channel ch's queue (entries are removed later by the service loop) |
| `0x44c720` / `0x44c700` | StopSound(obj) / StopSound(name) | `Stop()` one loaded sound (by name: looked up in the loaded list; no-op if not loaded) |
| `0x44c780` | StopAll(purge) | `Stop()` the head of every channel; if purge: delete **every** loaded sound regardless of use count and empty all queues |
| `0x44c850` / `0x44c880` | PauseAll / ResumeAll | Pause / Resume the head of every channel |
| `0x44c930` | FindLoaded(name) | loaded sound by name (case-insensitive) |
| `0x44cac0` | IsPlayingByName(name) | `IsPlaying` of the loaded sound (callers: grooming "brushing", "bubbles2") |
| `0x44c8b0` / `0x44c8d0` | Retain(name/obj, add) | use count ±1 |

`PauseAll`/`ResumeAll` are called when the application is deactivated/activated (`WM_ACTIVATEAPP`,
`0x44c000`) and by the script built-in `pauseNode` (`0x458f50`, `scripts.md`).

---

## 4. PlaySound (`0x44c3c0`) — 10 parameters, ~210 call sites

`PlaySound(name, channel, queue, preempt, loops, keep, volume, pan, startMs, stream)` → 1 on success,
0 on failure. Example: `PlaySound("dooropen", -1, 0, 0, 1, 0, 0x70, -1, 0, 0)`.

| # | Parameter | Type | Meaning | Values used on the disc |
|---|---|---|---|---|
| 1 | name | char* | WAVE chunk name (case-insensitive) | |
| 2 | channel | s16 | 0..15, or −1 = first free | −1 (almost all), 13 music, 14 voice, script-supplied |
| 3 | queue | int | 1 = append to the channel's queue even if busy ("QueueSound") | 0, 1 (QueueVoice) |
| 4 | preempt | int | 1 = stop everything on the channel and play now ("ForceSound") | 0, 1 (music, PlayVoice) |
| 5 | loops | int | 1 once, 0 forever, n times | 1; 0 for loops; 9999 for MusicBox |
| 6 | keep | s16 | 1 = add a reference that keeps the sound loaded after it ends | 0; 1 for ambient loops |
| 7 | volume | s16 | 0..127; −1 = leave Miles' current/default volume | 0x70 (112) default; 0x28 ambient; 0x30 music; others in §5.2 |
| 8 | pan | s16 | 0..127 (64 centre); −1 = default (63 for memory sounds, untouched for streams) | always −1 |
| 9 | startMs | s16 | start offset in ms | always 0 |
| 10 | stream | s16 | 1 = stream from disc instead of loading into memory | 1 only for music and "credits" |

### 4.1 Algorithm

1. `s = GetSound(name, keep, stream)`; return 0 if NULL.
2. If `queue` and `preempt` are both set: log `"You can't both preempt AND queue a sound! Queuing
   sound (%s)"` and clear `preempt`.
3. If not `queue` and `s.IsPlaying()`: count +1, `s.Stop()` (sends `SoundDone(name, 0)`),
   `Service(1)`, count −1. (Restart of a playing sound.)
4. If `channel == -1`: take the lowest channel whose queue is empty. If none:
   * neither queue nor preempt → log `"No free sound channels for sound %s. Try QueueSound or
     ForceSound to force playback."`, return 0;
   * else channel = 15, log `"No free sound channels to %s sound %s. Defaulting to channel %d"`
     (`%s` = "queue"/"preempt").
5. Store loops, volume, pan, startMs into the object (`0x44f6d0`).
6. If not `queue` and the channel is busy (`ChannelSound(ch) != NULL`):
   * not preempt → log `"Channel %hd not free for sound %s. Try QueueSound or ForceSound to force
     playback."`, return 0;
   * preempt → `StopChannel(ch)` and insert the sound **at the front** of the queue.
   Otherwise append it at the **end** of the queue.
7. Channel count +1, use count +1, `Service(0)` (starts it if it is at the head), return 1.

### 4.2 GetSound (`0x44c180`)

`GetSound(name, keep, stream)`: NULL if sound is disabled. If the name is already loaded, return
that object (count +1 if keep) — **regardless of whether it was loaded as a stream or memory sound**.
Otherwise create a memory (`stream == 0`) or stream object, copy the name, count +1 if keep, find
the WAVE resource (§8), and call `Load`. Failures: `"Can't find sound %s."`, `"Load error for sound
%s, reason: %s."` (with `AIL_last_error`); the object is deleted and NULL returned. On success the
object is appended to the loaded list.

### 4.3 Script interface

The script built-ins are documented in `scripts.md` ("Sounds"): opcode 0x3E
`playSound(wait, sound, channel, volume, queue, preempt)` → `PlaySound(sound, channel, queue, preempt,
1, 0, volume, -1, 0, 0)` and, if it succeeded and `wait`, `waitSound`; 0x3F `waitSound` (wake on
message 0x6A carrying that sound's name pointer, only if the sound is loaded and not stopped); 0x40
`stopSound`; 0x41 `stopChannel`; 0x42 `stopAllSounds` (`StopAll(purge = 0)`); 0x61 `pauseNode` also
pauses/resumes all channel heads. Scripts on the disc always pass channel −1, volume −1, queue 0 and
preempt 0 (`scripts.md`). The log texts "Try QueueSound or ForceSound" suggest those were the
script-level names of queue=1 / preempt=1 *(inferred)*.

---

## 5. Volume and pan

### 5.1 Ranges and defaults

* Volume 0..127, pan 0..127 with 64 = centre (Miles 4 scale; linear *(inferred)*).
* Memory sounds: `Play` passes volume/pan through `AIL_quick_set_volume`, which Miles stores in the
  HAUDIO and applies at every later play — **a volume set once persists for that loaded sound**
  until changed. With volume −1 nothing is set, so Miles' default (127, centre *(inferred)*) or the
  last value applies. When volume ≠ −1 and pan = −1 the wrapper passes pan **63**.
* Streams: volume and pan are set only when ≠ −1; Miles' stream default applies otherwise.
* One caller passes 250 (`"purrs"`, `0x42d590`); Miles' behaviour above 127 is unknown — clamp
  *(inferred)*.

### 5.2 Volumes used

| Volume | Use |
|---|---|
| 0x70 (112) | default for effects and every voice line |
| 0x30 (48) | music (stream) |
| 0x18 (24) | music while a voice line plays (ducked) |
| 0x28 (40), pan 0x40 | node ambient loops (`keep=1, loops=0`) |
| 0x20 (32), pan 0x40 | node ambient loop while a voice line plays |
| 0x32 (50) | "bubbles2", "horse1".."horse4" (random) |
| 0x19 (25) | "bookclose" |
| 0xFA (250) | "purrs" |
| `[0x80a110]` | race effects ("cstops", surface loops) — race-controlled variable |
| −1 | "credits" stream, "MusicBox", "falls"/"stream" (set afterwards, §5.3), sprite-frame sounds |

### 5.3 Direct per-sound volume (game code calling `AIL_quick_set_volume(sound+0x1C, vol, pan)`)

| Site | Sound | Rule |
|---|---|---|
| `0x41d270`, `0x41d330` | node ambient (+0x178) | (0x20, 0x40) when a voice line starts |
| `0x41d3f0`, `0x41d480` | node ambient | (0x28, 0x40) when the voice channel drains / voice is cancelled |
| `0x436c50` | trail ride "falls" (trail 3) and "stream" (other trails), `loops=0` | positional, below |
| `0x439e20` | trail ride "stream"/"falls" | (0, 0x40) = muted when the ride ends |
| `0x424390` | race surface loop | pan = 64 + dx/5 clamped 0..127; volume `[0x80a110]`, scaled when \|dx\| > 480 |

Trail ambient positional rule (`0x436c50`, `d` = current trail-movie frame from `0x436e80`; `trunc`
= C truncation, MSVC `__ftol`):

* **"falls"** (trail index 3): f = d·1.6933. If d ≤ 80: vol = trunc(f), pan = trunc(64 + 0.5·f).
  If d > 80: g = (d−80)·1.6933, vol = trunc(127 − 2g), pan = trunc(127 − g). Clamp vol ≤ 127,
  64 ≤ pan ≤ 127. If d < 160 apply, else `StopSound("falls")`.
* **"stream"** (other trails): d' = d + 40 − T[3·trail + (trail ≠ 0)], T at `0x7f3b40` =
  {525, 612, 665, 0, 0, 0, 0, 0, 0, 121, 199, 259, 156, 266, 383, 215}. If d' < 0 leave it. vol =
  trunc(d'·3.3967); if d' > 40: vol = trunc(127 − 2·(d'−40)·3.3967); clamp ≤ 112; pan 0x40. If
  d' < 160 apply, else `StopSound("stream")`. Both start at volume 0 (stream) / unset (falls),
  only while the ride's ambient flag (`+0x330`) is on. (Trail logic itself: see the trail docs.)

---

## 6. Voice (channel 14), ducking and lip-sync

Barbie's lines are WAVE chunks named `DBnnnn…` (also `db…`, `Db…`): 696 chunks, 670 of them 11025 Hz
8-bit mono, 26 at 22050 Hz.

### 6.1 Functions (Godiva node base)

| Address | Name (ours) | Behaviour |
|---|---|---|
| `0x41d270` | PlayVoice(name) | Duck: music (head of ch 13) `SetVolume(0x18)`; node ambient `AIL_quick_set_volume(0x20, 0x40)`. `PlaySound(name, 14, queue 0, preempt 1, 1, 0, 0x70, -1, 0, 0)` — interrupts the current line. voice-active = 1. Mouth flap on (`0x41d510(1)`). |
| `0x41d330` | QueueVoice(name) | Same ducking; `PlaySound(name, 14, queue 1, preempt 0, …)`; voice-active = 1. |
| `0x41d310` | QueuePauseVoice(name) | QueueVoice("pause"), QueueVoice(name) |
| `0x41d3f0` | VoicePoll | called every node idle while voice-active (`0x41cba0`): refresh the mouth flap; if `ChannelBusy(14)` is 0: music `SetVolume(0x30)`, ambient (0x28, 0x40), mouth flap off, voice-active = 0 |
| `0x41d480` | CancelVoice | if channel 14 busy → `StopChannel(14)`; then the same un-ducking |
| `0x41d3c0` | DelayVoice(name, ms) | store name/ms at +0x180/+0x184 and start the script **"DelayVoice"** (SCR chunk in common.omt); its init calls the node hook `"initDelayVoice"`, which sets script variable `delay` = ms. When that script ends, node vtable +0x90 (`0x41dc80`) plays the stored name with PlayVoice. |
| `0x41d250` | Delay(ms) | +0x190 = ms, start script "Delay" (hook `"initDelay"` sets `delay`) |
| `0x41e720` | SayHorseName | QueueVoice(`"DB0%d"` formatted with N): name index i (game state `+0x1C8[horse]`): i ≤ 9 → 631+i; i = 10 → 2055; 11 ≤ i ≤ 35 → 632+i; 36 → 2054; 37..39 → 668..670 |
| `0x41d8b0` | SayHorseName then QueueVoice(name) | |

Script string hooks (node vtable +0x98, `0x41dcd0`): `"voice <name>"` → PlayVoice(name);
`"SayHorseName…"` (only the first 6 characters "SayHor" are compared) → SayHorseName;
`"initDelay"`, `"initDelayVoice"` as above.

The silent WAVEs **"pause"** (1006 ms) and **"spause"** (560 ms) in common.omt/commonhd.omt are queued
between lines as gaps; `"Pause"`/`"sPause"` resolve to the same chunks (names are case-insensitive).

### 6.2 Completion callbacks

`SoundDone(name, completed)` (node vtable +0x80; base `0x41c460` → `0x457010`) posts **script event
0x6A** with the sound name to the event dispatcher (`[0x80a43c]`, vtable +4); scripts waiting on that
sound resume (§4.3). The `completed` flag is not forwarded to scripts. Node overrides chain dialogue
and transitions by name, e.g.:

| Override | Behaviour |
|---|---|
| `0x41c460` (Godiva node base) | "DB04158" ends → go to node "cottageL"; "DB9011" ends (flag +0x1BC) → "rabbitE"; then post event 0x6A |
| `0x403cd0` (applesort, vtable `0x7f0118`) | "db0112" → play "whinney2"; "whinney2" → PlayVoice("db04151"); "db04151" → flag + node switch (no event posted) |
| `0x420e70` (leapfrog, `0x7f1d78`) | "db9015" (+flag) → "bridgeS"; then base |
| `0x429cf0` (signin, `0x7f3420`) | "db0468" ends and `[0x80a144]` is set → "intromovie" (no event posted) |
| `0x42f3d0` (tackroom, `0x7f3640`) | "DB9059" → clears `[0x80a14c]`; then base |
| `0x43e6f0` (yard, `0x7f3fb0`) | "db9011" (+flag) → "rabbitE"; then base |
| `0x40e730` (bulletinboard, `0x7f0df8`) | "DB9011" (+flag) → "rabbitE", else forwards to a sub-object; then posts the event |
| `0x457010` (HCNode, logos) | post event 0x6A only |

### 6.3 Mouth flap (lip-sync)

There is no amplitude analysis: while channel 14 is busy, a looping "mouth flap" animation is shown
over Barbie and removed when the channel drains. `0x41d510(on)` picks it from the visible Barbie
animation: `"ambientBarbie"` in state 2 → `"mountFlaps"` (re-positioned to the Barbie sprite in the
yard), `"barbie_ambients"` in states 5..7 → `"walkFlaps"`, `"gameBarbie"` in state 0 →
`"gameFlaps"` (positioned in "bellgame"); the flap follows state changes on every poll. The stable and
tack room use their own `"MouthFlapStable"` / `"MouthFlapTackroom"` animations.

---

## 7. Memory sounds and streams

### 7.1 Memory sound Load (`0x44f840`)

1. Size = resource size (0 → `"Size is 0 for sound %s."`); allocate with `operator new`.
2. Read the whole WAVE chunk (a complete RIFF image) from the .omt.
3. `ConvertSoundSampleRate(&buf, &size, 22050)` (`0x44fd10`, §7.3). **Its return value is ignored.**
4. `AIL_quick_load_mem(buf, size)` → HAUDIO at +0x1C. Miles keeps the pointer (buffer freed only in
   the destructor after `AIL_quick_unload`).

### 7.2 Streams (`0x44fb40`)

1. Open a **second** `HCFileStream` on the same .omt file (`CreateFileA` GENERIC_READ,
   FILE_SHARE_READ) and seek it to the WAVE chunk's data offset.
2. `AIL_open_stream(dig, "\\\\\\\\" + decimal(HANDLE), 0)` — the 4-backslash prefix tells Miles to
   read from that file handle at its current position (BarbieRecomp §5.4).
3. Streams are **never sample-rate converted**; all streamed chunks are 22050 Hz 8-bit stereo.

For OpenBRC: a stream is just "play this WAVE chunk from the .omt incrementally"; the handle trick
has no visible behaviour beyond reading from the chunk offset and rewinding there on loop.

### 7.3 ConvertSoundSampleRate (`0x44fd10`)

Only a target of 22050 is supported (else log `"only converts 8-11 to 8-22"`, return 0). Checks, each
logging and returning 0 (buffer untouched) on failure: `RIFF` at 0, `WAVE` at 8, `fmt ` at 12. If
`nSamplesPerSec` (offset 0x18) == 22050 → return 1 unchanged. Otherwise the `data` tag must be **directly
after the fmt chunk** (offset 20 + fmtSize), else `"no 'data' tag"`, return 0.

Conversion (assumes 8-bit mono and a 44-byte header; nothing else is checked):

* N = data size. New buffer = old total size + N bytes. Copy the old bytes up to the data payload.
* Patch: RIFF size (+4) += N, nSamplesPerSec (+0x18) = 22050, nAvgBytesPerSec (+0x1C) = 22050,
  dword at +0x28 (data size in a 44-byte header) += N. Block align and bits are not touched.
* Samples (unsigned 8-bit, in = old, out at offset 0x2C):
  `out[0] = in[0]`; for i = 0 .. N−2: `out[2i+1] = in[i]`, `out[2i+2] = (in[i] + in[i+1]) >> 1`;
  `out[2N−1] = in[N−1]` — i.e. the 2N output samples are the linear interpolation shifted by one
  sample (in0, in0, ½(in0+in1), in1, …, ½(in[N−2]+in[N−1]), in[N−1]).
* `*size = N` — **the size returned is the original data size, not the new buffer size** (bug).
  Miles uses the size only for `AIL_file_type` (observed in `Mss32.dll` `0x20017fc0`) and plays from
  the RIFF headers, so the whole converted sound plays. An implementation must take the length from
  the RIFF/`data` chunk, not from this size (BarbieRecomp's HLE clamps to it and truncates converted
  sounds to about half — avoid that).
* The old buffer is freed and replaced.

Disc data (`snd_waves.py`) — what this means per format:

| Format (tag, ch, rate, bits, fmt size) | Chunks | Result when loaded as a memory sound |
|---|---|---|
| PCM, 1, 11025, 8, 16 (44-byte header, `data` right after `fmt `) | 675 | converted to 22050 as above |
| PCM, 1, 11025, 8, 16 with a `fact` chunk (56-byte header) | 13 | conversion fails ("no 'data' tag"), **plays unconverted at 11025 Hz** |
| PCM, 1, 22050, 8, 16 | 250 (one, "getfood", has a `fact` chunk) | unchanged |
| PCM, 2, 22050, 8, 16 | 41 | unchanged (music, ambient loops) |
| PCM, 1, 22050, 8, 18 (`fact`, 58-byte header) | 4 | unchanged ("pause", "spause" in two DBs) |
| PCM, 1, 22050, 16, 16 | 2 | unchanged |
| PCM, 2, 22050, 16, 16 | 1 | unchanged |
| PCM, 1, 44100, 16, 16 | 5 | would be garbled (treated as 8-bit mono) — these are `trails.omt` "trot0".."trot3", "walk", which **nothing references** (no exe string, no script) *(inferred unused)* |

Totals: 991 WAVE chunks in 26 .omt files (none in .omt.cache), 904 distinct names, 69.7 MB of
PCM, 4117 s. Header sizes: 973 × 44 bytes, 14 × 56 and 4 × 58 (18 chunks carry a `fact` chunk).
The 13 fact-chunk 11 kHz files are voice lines (`DB0158`, `DB9054a`, `DB9055a`,
`DB9056`, `DB9016n`, `DB04011a/b`, `DB04108`, `DB02057`, `DB9023`, `Db0078`, `Db0108a`, `DB04072`).

---

## 8. Finding a sound by name

`0x44dca0(type 'WAVE', name, &id)` walks the application's **open-database list** (`app+0xD4`) in
order and returns the first database whose `mCTy` name directory has a `WAVE` entry of that name
(case-insensitive, `0x451020`); then the chunk is located by id. The list order is the order the
databases were opened (`0x44d510(name, prepend, isCache)` appends unless prepend; non-cache files are
opened from the HD path and the CD path, HD first):

1. `commonhd.omt`, `common.omt` — opened once at startup (`0x411c70`);
2. then each node's own `<node>.omt` (opened by the node switch `0x44b4e0`) and the extra databases
   its constructor opens (e.g. stable: `stablewhite/dap/paint/pal.omt`; trail rides: `trails.omt`,
   `trlAnims.omt`, `trailPal/Wht/Pnt/Dpl.omt`; missions `mission*.omt`, `cert1.omt`).

`.omt.cache` databases are never in the list (they only preload animations) and contain no WAVEs.
So **common.omt / commonhd.omt win** over node databases for duplicated names. 83 names exist in more
than one database; only 6 differ in content and none of those involves common*.omt ("apple", "win",
"wrong", "null", "bridle", "db9023" — node databases that are not normally open together
*(inferred)*). The loaded-sound cache is keyed by name only, so a still-loaded sound of the same
name from a previous database would be reused; the purge at every node change (§10) prevents this.

---

## 9. Master volume and fades

* Master volume = `AIL_set_digital_master_volume` (0..127), also applying to Smacker audio
  *(inferred: same driver)*. The game never offers a volume control; it stays at Miles' default except
  during fades.
* Screen+sound fade `0x44df20(direction, targetVolume)` → `0x44df60` with DirectDraw, else
  `0x44e870` (instant: set the palette and the master volume to the clamped target).
* `0x44df60`: start = current master volume; step = (target − start) / `app+0xFC` + 1 (C integer
  division; 0 if target == start). 50 steps: direction ≠ 0 (fade out) counts the palette level 49→0,
  direction 0 (fade in) 0→49. Each step: palette scaled by level/50, volume += step clamped not to
  pass the target (fade out: not below, fade in: not above), then
  `AIL_set_digital_master_volume(clamp(v, 0, 127))`, then busy-wait until ≥ 12 ms have passed on the
  game timer `0x45db90`. Total ≈ 0.6 s.
* Node change (`0x44b4e0`, see `application.md` §3.3): V = current master volume; fade out to
  (`app+0x102` ? 0 : V), switch, fade in to V. `RequestNode(name, steps, fadeSound)` (`0x44de80`) sets
  `+0xFC = min(steps, 16)` and `+0x102 = fadeSound`. The logo nodes advance with
  `RequestNode(next, 16, 1)` (`0x421190`), so their transitions fade the sound out to 0 and back to V;
  `GotoNode` (`0x413370`, every game-node transition) passes `fadeSound = 0`, so the volume stays (step 0)
  and only the palette fades. GotoNode also saves the master volume to `[0x7ffa64]`, which nothing reads.
* Quit (`0x44b2e0`): fade out to 0 with the last `+0xFC` (normally 16: from 127 the step is −6, silent
  after ~22 of the 50 steps).

---

## 10. Music

Music is streamed on channel 13 by `PlayMusic(name)` (`0x4133c0`) =
`PlaySound(name, 13, 0, preempt 1, loops 0, keep 0, 0x30, -1, 0, stream 1)`, so a new track replaces
the old one immediately and tracks loop forever. All tracks are WAVE chunks (22050 Hz 8-bit stereo)
in common.omt / commonhd.omt (searched first) and duplicated in node databases:

| Track | Found in | Length |
|---|---|---|
| `sign-in` | common, commonhd | 33.3 s |
| `stablemusic` | common, commonhd | 10.5 s |
| `trailMusic` | commonhd, trails | 14.4 s |
| `nonmissionMusic` | commonhd, trails | 13.7 s |
| `hiddenMusic` | commonhd, trails | 10.5 s |
| `gameMusic` | common, commonhd | 16.0 s |
| `mysteryMusic` | common, commonhd | 17.4 s |
| `raceMusic` | commonhd, race | 20.2 s |
| `credits` (not music: credits narration, `PlaySound(-1 channel, loops 1, vol -1, stream)` in the credits node `0x410d20`) | credits | 54.6 s |

### 10.1 Selection on node change (`0x4133e0`)

Node vtable +0x4C (`0x41e8d0`) is called on the **old** node by the node switch with the **new**
node's name. It always does `StopAll(purge=1)` (every sound stops and is unloaded), `Service(1)`,
`StopChannel(13)`, then, unless music is off (Godiva app `+0x344` ≠ 0):

| New node | Track |
|---|---|
| signin | sign-in |
| yard, appleW, rainbowS, beachE, bridgeS, race | trailMusic |
| stable, grooming, tackroom, bulletinboard | stablemusic |
| rabbitE | nonmissionMusic if app `+0x254` == 0, else trailMusic (`+0x254` is cleared by the yard's debug "Tutorial on" key — *inferred*: 0 while the tutorial is running) |
| applesort, vinegame, leapfrog | gamemusic |
| wishingL, cottageL | hiddenmusic |
| anything else | none (music stays stopped) |

So the music restarts from the beginning on every node change. The logo nodes (MattelLogoNode,
HCLogoNode, BarbieSoftLogoNode; vtables `0x7f1e88`, `0x7f1828`, `0x7f02a8`) derive from HCNode, whose
+0x4C is empty: leaving them neither purges sounds nor starts music. The start-up chain is logos →
preferences → signin (`application.md` §2.2), so "sign-in" starts when preferences hands over to
signin. bulletinboard (`0x40eb60`) and tackroom (`0x42d380`) wrap the hook (extra state when moving
between those two) and then call it.

### 10.2 Other music changes

* Trail ride (`0x437e50`): if the trail music flag `[0x802798]` is 0 → stop channel 13. Else wanted =
  1 if story state `+0x310` == 0x11 or (== 0x10 and `+0x300`); else 4 if `[0x80a160]`; else per trail
  `[0x7f3c30 + 4·trail]` = {2, 3, 2, 2, 2, 2, 3, 3}. Only when different from the current one (and
  music on): StopChannel(13) if a track was set, then 1 → nonmissionMusic, 2 → trailMusic,
  3 → hiddenMusic, 4 → MysteryMusic.
* Race (`0x424390`): raceMusic. Trail events `0x441700`, `0x442af0`, `0x442d30`, `0x443130`:
  trailMusic / mysteryMusic / gameMusic.
* Carousel (`0x410540`): `PlaySound("MusicBox", 13, 0, 0, 9999, 0, -1, -1, 0, 0)` (memory sound,
  9999 loops) and PlayVoice("DB9045").
* The music-off flag: preferences node `0x422300` sets Godiva app `+0x344` = 1 when the "music off"
  radio button (`+0x1E4`) is selected and saves it with the preferences (format: preferences docs).
  Trail rides also have the debug toggle `[0x802798]` ('M'/'m' in the yard with debug mode on) and
  force it to 0 on low-end machines (`movies.md` §4.1).

---

## 11. Ambient loops

Each node may start one ambient loop with `PlaySound(name, -1, 0, 0, loops 0, keep 1, 0x28, -1, 0, 0)`
and store the name at node `+0x178`; `0x41d870` stops and releases it (`StopSound` +
`Retain(name, 0)`) in the destructor or before switching.

| Node | Loop |
|---|---|
| applesort | wind |
| bellgame | waterfall |
| grooming, tackroom | stableamb2 |
| stable | stableamb |
| leapfrog | frogloop |
| yard | exterior |
| vinegame | falls |
| race | per-surface `xtrr001…` table `0x8015a0` |
| trail ride | own field +0x4B8 (per-trail table) and the positional "falls"/"stream" (§5.3) |

Animation frames can trigger sounds: HCAn frame flag 0x10 → `PlaySound(frameSound, -1, 0, 0, 1, 0,
-1, -1, 0, 0)` (`0x4497b0`, see `animation.md` §5.5).

---

## 12. Notes for the implementation

* Model: 16 FIFO channel queues of named sound objects; one object per name; use counts; a service
  pass per frame (start queued heads, poll for the end, notify, dequeue one, unload count-0 sounds).
* Reproduce Miles semantics: volume/pan set before play persists; loop count 0 = infinite; status
  "done" only after the last loop; pause keeps position and remaining loops.
* Mixer: 22050 Hz output; play 11025 Hz unconverted voices at their own rate; implement the 2×
  interpolation exactly if bit-exact output matters, otherwise any resampler (the converted data is
  what the original plays).
* Completion matters for game flow: `SoundDone` must fire for natural ends (and with completed = 0
  for stops), and only from the service pass.

## 13. Open questions

* Miles' exact behaviour for volume > 127 (`"purrs"` at 250) and its pan law.
* Whether the trail-ride music and ambient flags also gate the event-specific tracks in `0x441700`…
  `0x443130` (only the `+0x344` music-off flag was checked there).
