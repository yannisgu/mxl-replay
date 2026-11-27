# Concept: PGM/PRV mode

Status: draft for discussion, not specified or built. Once agreed, it becomes a section of
SPECIFICATION.md (§5.6 for the engine, §8.2a and §8.9 for the UI and operators).

## 1. Why

Today an operator prepares a replay on the channel that is on air. The vision mixer cuts
away, or the operator works quickly while the audience watches. With two channels, the
director has to cut between `LSM A` and `LSM B` on the mixer, and the operator has to keep
track of which one is on air.

An LSM's PGM/PRV mode puts two channels behind one on-air output:

- **PGM** (programme) is the channel the vision mixer takes. Its sender never changes.
- **PRV** (preview) is where the operator prepares the next shot: scrub, angle, IN, speed,
  cue a clip. Nobody downstream has to see it.
- **Take** puts the PRV shot on PGM with a cut or a mix, frame-accurately, inside the
  replay. The mixer does not have to cut.

Typical sequence: PGM is live or holding a frame. The operator finds the action on PRV and
cues it at IN, angle 2. The director says "roll". The operator presses Take (or the red
light does it), and PGM mixes into the replay at 50 %. Then PRV is ready again at the same IN,
so the operator presses 3 for the reverse angle and takes again.

### 1.1 What EVS LSM does

From the MulticamLSM 10.01 operating manual (July 2009), chapter 6 "PGM-PRV Mode",
and the LSM-VIA user manual 1.1 (2021). The manuals do not describe TAKE as a complete
procedure, so the rules in §4 for what moves and what PRV does afterwards are ours.

| EVS | What it does | Here |
| --- | --- | --- |
| 1PGM+PRV mode, "at least 2 playback channels"; OSD `PGM1 CAM A`, `*PRV1* CAM B` | Two channels, one on air | Pair (§4.1) |
| Transition button: Mix, Wipe L>R, R>L, U>D, D>U, Cut, "between the PGM and PRV pictures"; duration in Setup ("Effect Duration for Take") | The TAKE effect | Take transition (§4.2); wipes in phase 2 |
| **PRV CTL** off: "controlling both PGM and PRV". On: "control of the PRV with the jog dial and most buttons, while the lever and the Play button will control the PGM output"; clips "call them up on the PRV side" | Split control | Control modes (§4.4) |
| Primary channel: "the first controlled channel", PGM with PRV CTL off, PRV with it on; marked with stars | Which channel the screen follows | `primary` (§8.9) |
| **SW to IN**: on PRV, "a camera change will cause a jump to the corresponding IN point"; then "via the TAKE button, can auto-chain cameras from the same IN point on the PGM output" | Angle sequences | *Switch to IN* (§4.3) and `recue` |
| **Sync PRV**: "synchronize the PRV with the timecode and speed of the PGM output" | PRV catches up with PGM | *Sync PRV* (§4.3) |
| **Rst Cam**: "CAM A on PGM1, CAM B on PRV" | Back to the default angles | *Reset cameras* (§4.3) |
| **PgmSpd**: the lever gives only 0 % or the setup speed on PGM; PLAY flashes red | No accidental speed on air | Candidate (§7) |
| LSM-VIA: OSD `LEVER CTRL` on the PGM monitor, `FULL CTRL` on PRV | What each monitor is controlled by | Monitor and multiview captions (§6) |
| Multi-PGM: "not possible to create a transition between the channels in this mode" | Several PGMs, no take | Out of scope; `controlled` stays a list (§8.9) |

The search results gave 5 frames as the default for "Effect Duration for Take". The
configuration manual itself was blocked, so that default is unverified.

## 2. Terms

- **Pair**: a PGM channel and its PRV channel. A channel is in at most one pair.
- **Shot**: what a channel shows and how: camera, position, speed, play state (live,
  playing, paused, cued, ended), marks, clip or playlist entry, motion and audio modes.
- **Take**: PGM adopts PRV's shot, with a **take transition** (`cut` or `mix`, frames).
- **After take**: what PRV does once its shot is on PGM (§4.3).

## 3. Decision: fixed outputs, the shot moves

The PGM channel keeps its NMOS/MXL senders. At a take, the engine moves the PRV shot's state
into the PGM channel and renders the old PGM shot as the outgoing side, as it already does
for a playlist mix (§7, `ChannelRuntime::Mix`).

Rejected alternatives:

- **Flip-flop** (the two channels swap roles at each take). The on-air sender would change
  at every take, so the vision mixer or router would have to follow. Tally, NMOS routes and
  the multiview labels would change with it. That is the problem this mode exists to solve.
- **Take in the UI** (the browser sends PRV's camera, position and speed to PGM as normal
  commands). A take would land on an arbitrary grain and could not mix. A second client
  would see an intermediate state.

## 4. Behaviour

### 4.1 Pairing

- `CH<n>_PREVIEW=<m>` on the PGM channel (default empty: no pair). Settings,
  `POST /api/v1/channels/{n}/preview {"channel": m}` (`0` clears it). It is kept in
  `REPLAY_STATE_DIR/config.json` and in the config export, like the idle playlist. Pairing
  applies at once, with no restart, because it changes no sender.
- Refused with 400 and the reason: `m == n`, a channel outside 1..`REPLAY_CHANNELS`, a
  channel already in a pair, two channels with different owners (§8.9; two unowned
  channels may pair), and a lock (§5.2) set in Settings on either channel. While the pair
  exists, Settings cannot lock either channel. The pair's `both` control mode uses the lock
  internally (§4.4).
- Labels do not change. The role shows as a badge (`PGM`, `PRV`) in the UI, the widgets and
  the multiview captions.

### 4.2 Take

`POST /api/v1/channels/{pgm}/take {"transition": "cut"|"mix", "frames": 1..250,
"play": bool, "after": "recue"|"swap"|"live"|"keep"}`. Every field is optional, and a
missing field takes the pair's defaults (`CH<n>_TAKE_TRANSITION` `mix`,
`CH<n>_TAKE_FRAMES` 10, `play` true, `CH<n>_AFTER_TAKE` `recue`). It also exists as the
transport command `take` and the `POST /api/v1/control {"action":"take"}` path.

At the next PGM grain, the engine:

1. takes a snapshot of PRV's shot (the scheduler, live mode, marks, clip, playlist and
   index, own audio, modes);
2. moves the current PGM shot into the outgoing side (`mix`) when the transition is a mix:
   - playing: it plays on at its speed, past an OUT it holds there;
   - paused, cued or ended: it holds its frame;
   - live: it stays live on its camera (the side reads the newest frame, like a live
     channel);
3. gives PGM the PRV shot. With `play` and a PRV shot that is cued or paused, PGM plays
   from that position at PRV's speed. A playing shot continues at the same position, so a
   take of a rolling PRV is seamless. A live PRV makes PGM live on that camera.
4. renders the transition as a playlist mix: grain `k` of `N` shows `(k+1)/(N+1)` of PGM's
   new shot over the outgoing one. The audio crossfades with equal power over the same
   frames (2 frames on a cut, as a playlist cut). No side is interpolated inside the mix.

The answer has the status and `take_ns`, the TAI of the grain where the take starts. A
take is logged as `take` and counted in `takes_total{channel,kind}`.

Refused with 409 and a reason that the UI shows: no pair (`not_paired`) or a PRV with
nothing to show (`prv_empty`: stopped, black, idle playlist).

A take during a running take finishes the running one at once, as a cut to its incoming
shot. It does not start a second one, so a nervous double press does no harm. A take while
PGM is inside a playlist transition finishes that transition first, the same way.

### 4.3 After take

What PRV does once its shot is on PGM (`after`):

| `after` | PRV after the take | Used for |
| --- | --- | --- |
| `recue` (default) | the same shot, paused at the position it was taken from (IN for a cued clip) | the next angle of the same action: press 3, take again |
| `swap` | the old PGM shot, as it was at the take | going back, or comparing |
| `live` | live, on its camera | following the game for the next action |
| `keep` | nothing changes (a playing PRV plays on) | showing the same pictures twice on purpose |

`recue` keeps the angle, the marks and the speed, so the operator only changes what
differs.

PRV helpers, after EVS:

- **Switch to IN** (`CH<n>_SWITCH_TO_IN`, default true on a PRV): an angle change on a
  paused or cued PRV that has an IN jumps to that IN, so camera 3 is ready at the start of
  the action. Without an IN, or with the option off, the angle changes at the same
  position, as today. Together with `recue`, this gives EVS's auto-chain: take, press 3,
  take, press 4, take.
- **Sync PRV** (`POST /api/v1/channels/{pgm}/sync-prv`, key `S`): PRV jumps to PGM's
  position and speed (and play state) on its own camera. Use it to show another angle of what is on
  air, or to catch up before the next shot.
- **Reset cameras** (`CH<n>_PGM_CAMERA` / `CH<n>_PRV_CAMERA`, key `R`): back to the pair's
  default angles. Live channels stay live on them.

### 4.4 Control

The operator's `controlled` list (§8.9) gets a meaning for a pair, after EVS's PRV CTL.
The pair's **control mode**:

| Mode | `controlled` | Jog, scrub, marks, angle, cue, clip | Speed fader / lever, Play/Pause, Live |
| --- | --- | --- | --- |
| `prv` (EVS PRV CTL on; default) | `[prv, pgm]`, primary PRV | PRV | PGM |
| `both` (EVS PRV CTL off) | `[pgm, prv]`, primary PGM | PGM, and PRV follows its position on its own camera (as a lock, §5.2) | PGM, and PRV follows |
| `pgm` / `prv-only` | the one channel | that channel | that channel |

- `prv` is the replay operator's normal mode. They prepare on PRV with the jog, and the
  lever rides the speed of what is on air, with no key press between take and lever.
  This is the split that §8.2a's "every key acts on the channel under control" rule does
  not yet allow. The rule becomes: every key acts on the channel its *row* says. The
  key overlay shows two columns, PRV and PGM.
- `both` replaces the lock refusal of §4.1 for a pair. A lock is how `both` works, and
  it is set by the control mode, not in Settings.
- A key (default `V`, *PRV CTL*) toggles `prv` ↔ `both`, Tab steps through the operator's
  other channels as today. The mode changes only by an explicit action, never by a take,
  tally or auto-advance (as control in §8.9). It is kept as `CH<n>_PAIR_CONTROL`.
- API: `POST /api/v1/operators/{o}/control {"channels": [2, 1]}` stays the source of
  truth. A pair's two channels in that order are the mode, so nothing new is needed in the
  operators API. `GET /api/v1/operators/{o}` adds `pair_control`.

### 4.5 Tally

- PGM gets the channel's TSL tally as today. PRV is not on air, and the UI shows it green
  (the preview colour) whatever TSL says.
- **Take on red** (`CH<n>_TAKE_ON_TALLY`, default false, Settings): on the rising edge of red
  on PGM with a PRV shot that is cued or paused, the replay takes with the pair's defaults
  and plays. It replaces *play on red* (§8.8) for that channel while the pair exists. A
  PRV with nothing to show falls back to *play on red* for PGM's own shot.

### 4.6 Playlists

- Phase 1: a playlist plays on whichever channel it was started on. A take of a PRV on a
  playlist entry moves the playlist context with it, so PGM auto-advances on through the
  playlist, and with `recue` PRV cues the same entry again.
- Phase 2, **look-ahead**: while PGM plays a playlist, PRV follows it and shows the next
  entry cued at its IN (read-only). A take advances PGM to that entry now, with the take
  transition instead of the entry's. Touching PRV (scrub, angle, cue) detaches it.

### 4.7 Shotbox and widgets

- The shotbox and `clip-list` target PRV when their channel is a PGM with a pair: the first
  click cues on PRV, the second click takes (`play` true). Option: "first click to PGM"
  for the old behaviour.
- `transport` on a PGM with a pair gets *Take* and the transition selector. On PRV it gets
  the role badge.

## 5. Engine

- `ChannelRuntime::Mix` today holds the outgoing playlist entry and steps it to its OUT. It
  must also hold a shot without an OUT (play on), a held frame, and a live side (camera,
  newest frame). Taking the snapshot in step 1 needs both channels' locks for one grain
  boundary. The engine takes them in channel order.
- The take is a command on PGM's playout thread, applied at its next grain. PRV's state is
  read under the lock at that grain, not when the request arrives. PGM and PRV have their
  own playout threads, so the snapshot can be up to one grain ahead of PRV's last
  rendered picture. That is acceptable for a take; it needs a test.
- Decode ahead (§5.1): while PRV is cued or paused, PGM's plan also has PRV's current frame
  and the first frames of its play (within the 8-frame plan). A take of a cued shot is then a
  `hit` from its first grain. A take of a playing PRV may miss the first 1–2 grains, as
  today after a jump.
- Flow ahead (§5.3): no change. The incoming shot is not interpolated inside a mix. After
  the mix, PGM's flow worker plans as after a cue. With `interpolate`, a take of a
  slow-motion shot can fall back to `blend` for 1–3 grains after the mix, as at the start of
  a play.
- Cost: a mix decodes two pictures per grain for its `N` frames, as a playlist mix. Nothing
  more outside a take.
- Status per channel: `pair` (`null` or `{role: "pgm"|"prv", partner}`), PGM adds
  `take_defaults` and `take_on_tally`. The running take is the existing `transition`
  object with `source: "take"` (`"playlist"` for the existing ones). Metrics:
  `takes_total{channel,kind}`, `take_prv_empty_total{channel}`.

## 6. Operator screen (§8.2a)

A pair is shown as one unit: PGM on the left, PRV on the right, and the take bar between
them.

```
┌─────────────── PGM · LSM A ───────────────┐ ┌────────────── PRV · LSM B ───────────────┐
│                                            │ │                                          │
│            (red border on air)             │ │       (green border, blue header:        │
│                                            │ │        the primary)                      │
│  PLAY 50 %  Cam 2   10:31:04:12   −0:06    │ │  CUE  Cam 3   10:30:58:00   IN ▸ OUT 4 s │
│                             LEVER CTRL     │ │                              *FULL CTRL* │
└────────────────────────────────────────────┘ └──────────────────────────────────────────┘
  ▸ playback strip (PGM)        ┌──────────────────────┐   ▸ playback strip (PRV)
                                │ MIX 10 ▸  (T cycles) │
                                │  [      TAKE      ]  │
                                │ after: recue ▾  SWAP │
                                │ PRV CTL ● SYNC  RST  │
                                └──────────────────────┘
```

- **TAKE** is the largest button on the screen. It shows the progress of a running take
  and is disabled with the reason when PRV is empty.
- Each monitor states what controls it, as the LSM-VIA OSD does: `FULL CTRL` (jog and
  keys) or `LEVER CTRL` (speed and Play only), with the primary in stars.
- The transition is one button that cycles (Cut, Mix, and the wipes once they exist), as
  on EVS. The duration is a setting with quick picks 5, 10, 25 and 50.
- Keys: `T` take with the selected transition, `Shift+T` take as a cut, `X` take with
  `after: swap`, `V` PRV CTL, `S` Sync PRV, `R` Reset cameras. In `prv` mode, Space, `L`
  and the speed keys act on PGM, and the jog, ←/→, I/O, [ ], angles, `C` and Enter act on
  PRV (§4.4). The ShuttlePRO button is configurable (default: the button under the jog),
  and the shuttle ring is the lever on PGM.
- A channel that is in no pair is shown as today, so an operator with three channels can
  have a pair and a free channel.
- The camera row follows the primary, normally PRV, so the operator picks the angle at the
  PRV position. The tiles mark PGM's camera in red and PRV's in green.

Multiview (§8.9): channel captions get the role, the header reads
`… · FOLLOWING PRV LSM B`, a running take shows `TAKE MIX 10 · 4/10`, and the captions
carry `LEVER CTRL` / `FULL CTRL` as on the operator screen.

## 7. Phases

1. Pairing, take (cut and mix), `after` (`recue`, `swap`, `live`, `keep`), the control
   modes `prv` and `pgm`/`prv-only`, Switch to IN, Sync PRV, Reset cameras, the status, the
   API, the operator screen's take bar and keys, and Settings. Unit tests for the
   snapshot, the outgoing side (play on, hold, live), double take and refusals. An
   integration test: take a cued PRV with a mix, then check PGM's pictures against both
   sources grain by grain (the playlist mix test as a model).
2. The `both` control mode (PRV locked to PGM on its own camera), take on red, playlist
   look-ahead, shotbox and widgets targeting PRV, decode ahead of the PRV shot on PGM, and
   the four wipes (EVS's L>R, R>L, U>D, D>U; §13 lists wipes). A wipe is a moving
   split of two pictures with a soft edge, and it costs as much as a mix: two decodes, no
   flow.
3. Candidates: *Program speed* (EVS PgmSpd: in `prv` mode the lever on PGM gives only 0 %
   or one set speed, so a nudged lever cannot change the speed on air), post-roll after OUT
   on PGM, several PGMs controlled together (EVS Multi-PGM, which has no take), take of the
   PRV's playlist as a whole.

## 8. Open questions

1. Settled by EVS: control after a take stays as it is. In `prv` mode the lever already
   acts on PGM and the jog on PRV, so nothing has to move. Still open: is the split right for
   our keyboard-and-mouse operators, or only with a ShuttlePRO? Proposal: the split in both
   cases, and a key overlay with two columns.
2. Default take duration: EVS reportedly ships 5 frames (unverified). This draft says 10,
   short enough to feel like a take and long enough to read as a mix at 50p.
3. Default `after`: `recue` (proposed) or `live`? It depends on whether our operators work
   angle sequences or follow the game more.
4. Should a pair also exist for automation channels, with no operator? It costs nothing,
   but nothing would use it except the API.
5. Do the NMOS labels of a paired channel change (`LSM A PGM`)? Proposal: no, the role is a
   runtime setting and labels need a restart.
