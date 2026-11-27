# Concept: speed fader, clip speed and playlist entry editing

Status: agreed in discussion (§7), not specified or built. Prototype:
[docs/prototypes/lsm-clip-editing.html](../prototypes/lsm-clip-editing.html). Once agreed, it
becomes part of SPECIFICATION.md §5.2 (the fader), §7 (entries, clip copies) and §8.2a (the
operator screen).

## 1. Why

On the operator screen (§8.2a) an operator can play a clip and build a playlist, but cannot
shape either:

- **Speed** has five preset buttons (25–100 %). There is no fader, although §5.2 asks for a
  continuous one. An operator who wants 40 %, or a ramp by hand, cannot get it.
- **A clip's speed** is the channel's target speed at the moment of C. To change it, the
  operator has to go to the main UI's clip inspector (§8.2). That inspector stays where it
  is; the operator screen does not get one.
- **A playlist entry** has its own speed, end action, auto-advance and transition into it
  (§7). The `playlist-editor` widget can edit them, but the operator screen's sidebar can only
  add, reorder and remove entries.
- **A clip's IN/OUT** can only be edited in the main UI's inspector. From a playlist there is no way to
  trim the clip an entry plays. There is also no way to trim it for this playlist only, when
  the clip is used elsewhere as well.

## 2. Terms

- **Channel speed**: the speed the channel plays at now (`target_speed`; `POST
  /api/v1/channels/{n}/speed`, ramped over 3 frames, §5.2). This is what the fader sets.
- **Clip speed**: the speed a clip starts at when it is cued (`ClipRef::speed`, 1–200 %).
- **Entry speed**: the speed a playlist entry plays at. It is either **null** or a number:
  - null means the entry **follows its clip's speed**, now and after later edits. A new entry
    is null.
  - a number is the entry's **own** speed, independent of the clip.
- **Mix style**: the transition **into** an entry: `cut`, or `mix` over `transition_frames`
  (1–250 **frames**), with its audio crossfade (`audio_frames`, −1 = follow the mix).
- **Clip copy**: a new clip with the same camera, range, speed and modes as another, and
  `copy_of` pointing to it. It is an ordinary clip: it has its own IN/OUT, its own protected
  range and its own button in the bin.
- **Entry id**: a stable id per playlist entry (`entry_id`), so that one entry can be
  addressed while others reorder.

## 3. Decisions

### 3.1 One rule for edits: prepared shots take them, shots on air do not

An edit to a clip or an entry changes what is stored. A channel follows the edit only if it
has the clip or entry **cued and not yet played**, because that shot is being prepared. A
channel that is **playing** the clip or entry, or paused inside it, keeps what it was cued
with. The monitor then shows "Saved · applies at the next cue".

So nobody's on-air picture jumps because someone touched an editor. The fader is the only
live control of a playing channel.

### 3.2 The fader is the channel speed; the presets stay

- A horizontal slider, **0–100 % by default**. A setting of the operator screen extends it to
  **0–200 %**. The setting is kept **in the browser** (local storage), per screen. Then 0–100 % keeps two thirds of the travel, so slow motion stays precise.
  Reverse speeds are not on the fader.
- Ticks mark 25, 33, 50, 75 and 100 % (and 150, 200 % in the extended range). Within ±2 % of
  a tick the fader **snaps** to it. Holding Alt while dragging turns snapping off.
- **Mouse only, no keys** for now: drag the fader, use the wheel over it (±1 %, Shift ±10 %),
  or click a preset.
- The value is shown as a number next to the fader (`42 %`) and in the monitor's OSD.
- While dragging, the screen sends at most 20 commands per second, and always the last value
  (latest wins). The engine's 3-frame ramp smooths the steps.
- On a cued, paused or ended channel, moving the fader also plays, as a lever does. On a live
  channel the fader shows 100 % dimmed. Moving it below 100 % leaves live at the current
  position and plays at that speed.
- **Save to clip / Save to entry**: when the fader's speed differs from what the shot was cued
  with, a button next to the presets stores it:
  - as the clip speed, when a clip plays on its own;
  - as the entry's own speed, when a playlist entry plays.

  Operators find the speed by hand on air, then keep it.

### 3.3 Clip and entry speed, as on an EVS LSM

There is no clip editor. On an EVS LSM the **lever** sets the speed, and **ENTER** stores it.
In Playlist Edit mode, the operator selects an element, moves the lever until the speed shows
in the element's field, and presses ENTER. The lever also sets the effect duration. **Edit
all** (SHIFT + D, then the speed parameter, then the lever) sets every element of a playlist
at once. A setting gives new elements a default speed; *Unknown* keeps the previous
element's speed. These are from the Multicam LSM operating manuals and the LSM-VIA user
manual. Key names differ between versions.

Here:

| EVS | Operator screen |
| --- | --- |
| Lever | The fader (§3.2) |
| Lever, then ENTER on the loaded clip or element | **Save 42 % to clip / to entry** next to the presets (§3.2) |
| Element speed field in Playlist Edit | The **speed badge** of an entry: a click opens presets |
| Edit all → speed | **All speeds** in the playlist head |
| Default speed / Unknown | **Clip**: the entry follows its clip's speed (`null`, §2) |

- **The speed badge.** Every clip card and every entry row shows its speed as a small button.
  A click opens a popover with the presets: 25, 33, 50, 75 and 100 %, plus 150 and 200 % in
  the 0–200 % range. For an entry it also offers **Clip 33 %**, which makes the entry follow
  its clip. A dashed badge with a link icon marks an entry that follows. Any other speed is
  set the EVS way: play it on A, move the fader, Save.
- **All speeds** sets every entry of the playlist to a preset or to Clip.
- Every change is saved at once (`PATCH /api/v1/clips/{id}`, the entry's `PATCH`) and follows
  §3.1. A clip speed change also changes every entry that follows the clip.

### 3.4 Entry editing in the sidebar

An entry row in the sidebar expands when it is clicked (one at a time; Esc collapses it).
The expanded row contains:

- **IN/OUT** of the clip the entry plays, shown only (§3.5), and **Make a copy**.
- **End** and **Auto-advance**.
- **Cue on A** and **Remove**.

The speed is not in the expanded row. It is on the row's speed badge (§3.3).

The **mix style** sits on the **join** between two entries, as in an edit list. Between every
two rows there is a pill: `Cut` or `Mix 25`. The first entry's pill is shown only when the
playlist loops. A click opens it inline:

- **Cut** or **Mix**;
- for a mix, chips for 12, 25 and 50 frames and a number field (1–250). The length is always
  in **frames**;
- (later) the audio crossfade, which follows the mix by default.

Every change is saved at once. Edits to the entry that is playing follow §3.1. An edit to the
next entry applies if its transition has not started yet.

### 3.5 IN/OUT: cue it, then mark as always

There is no special trim editor and no save step. A clip's IN/OUT are changed on a channel,
with the controls the operator already uses for every shot:

1. **Cue** the clip (double-click in the bin) or the playlist entry (Cue on A from the opened
   entry). The engine sets the channel's marks to the clip's IN and OUT on a cue.
2. **Scrub and mark** as always: the timeline under the monitor (zoomed to the clip ±10 s,
   the stored range dashed), the wheel on the monitor, ←/→ (Shift: 1 s), Go to IN/OUT
   (`[` `]`), and **IN**/**OUT** (`I`, `O`).
3. **Each mark is stored in the clip at once.** The toast says what changed and where else
   the clip is: "IN 14:07:11:48 · Save 18' · also in Best saves". OUT before IN is refused,
   and the clip stays as it was.

So with a clip loaded, IN and OUT mean the clip's IN and OUT, as long as the marks stay
within 10 s of it (the zoomed timeline). An operator who scrubs further away to mark a new
action is on a new shot: the loaded clip is left alone.

**OUT makes the clip.** Without a loaded clip (or away from it), OUT after an IN makes the
clip from the marks and cues it on the channel, paused at its IN, as a double-click
would. The usual sequence is IN, OUT, Space. From then on the clip is loaded, so further IN
and OUT trim it. There is no C key or Clip button any more: OUT makes every clip.

To change **one entry only**, the operator makes a copy **first**: the opened entry's **Make
a copy** (the entry has to be on a channel). Then IN and OUT change only the copy. The opened
entry says so when its clip is used elsewhere: "The clip is used elsewhere too: Make a copy
first to change only this entry."

**Make a copy** does two things:

1. It creates a clip copy named after the clip and the playlist: "Save 18' (Highlights 1st
   half)", then "(Highlights 1st half 2)" if that name exists. A copy of a copy takes the
   original's name.
2. It points the entry to the copy and cues the entry again (at the copy's IN, unless the
   channel plays), so the channel has the copy loaded and later IN/OUT change only the copy.

The copy is in the bin with a COPY tag, so it can be reused, renamed or deleted like any clip.
Later edits of the original do not reach the copy, and the entry says "This entry's own copy of
“Save 18'”".

**Deleting the original keeps its copies.** They stand alone with their own protected ranges.
Their `copy_of` keeps the deleted id, and the screen says "the original is deleted". A clip
that is in a playlist cannot be deleted from the operator screen. It has to be removed there
first.

A copy of a buffer clip protects its own range. Two ranges over the same frames already work:
`FrameRing` keeps a list of ranges, and deleting one keeps the other (`ring.cpp`,
`unprotect`). Extending a copy beyond the original counts against the protection cap like any
clip edit (409, `force`).

## 4. API

| Change | Where |
| --- | --- |
| Channel speed | `POST /api/v1/channels/{n}/speed` (exists) |
| Clip speed, IN/OUT | `PATCH /api/v1/clips/{id}` (exists; 409 at the protection cap with `force`) |
| Entry speed `null` | `speed` per entry may be `null` (new). Absent on `POST` means null. Answers give `speed` (effective) and `own_speed` (`null` or the number) |
| Entry ids | `entry_id` per entry (new), made by the server, kept across `PUT`s that send it back |
| One entry | `PATCH /api/v1/playlists/{id}/entries/{entry_id}` (new): speed, end, auto-advance, transition fields |
| Copy for an entry | `POST /api/v1/playlists/{id}/entries/{entry_id}/copy-clip` (new): makes the copy, points the entry to it, answers the copy and the playlist |
| Copy origin | `copy_of` on a clip (new; `""` when it is not a copy), in the answers, the catalog and the config export |

- **Old playlists**: their entries have numbers as speeds, so nothing changes when they play.
  To make an existing entry follow its clip, the operator uses "Follow the clip".
- **Entry ids** make two operators safe as long as each uses `PATCH` on one entry. A `PUT`
  with the entries still replaces the list. It keeps the ids that are sent back and gives new
  ids to entries without one. Two `PUT`s still race, so the operator screen uses `PATCH` for
  everything except reordering, adding and removing.

## 5. Engine

- **Entry speed null**: wherever playout takes `entry.speed` (cue, `planLengths`, the
  countdowns, the transition start), it takes `entry.speed.value_or(clip.speed)`. The catalog
  stores `null`. The config export writes `null`.
- **Entry ids**: a string per `PlaylistEntry`, unique within the playlist (`e1`, `e2`, …),
  made on `POST`, on `PUT` for entries without one, and when an older catalog is loaded.
  `updatePlaylist` already keeps a channel on its entry by clip id; with ids it can match the
  entry exactly.
- **Copies**: `copyClip(id)` is `createClip` with the same fields plus `copy_of`. It protects
  the same range, an index lookup with no frames read. `copy-clip` does the copy and the entry
  change under one lock, so a channel never sees an entry pointing to a missing clip.
- **Re-cue on edit (§3.1)**: `updateClip` and `updatePlaylist` re-cue the channels that are
  `Cued` on the edited clip or entry (new IN, speed and modes). Playing or paused channels keep
  their shot.
- **Fader traffic**: nothing new. `setSpeed` already ramps.

## 6. Phases

1. **Fader and speeds.** The fader (0–100 %, the 0–200 % setting, snapping, wheel,
   presets), Save to clip/entry, the speed badges and All speeds, and the IN/OUT bar under the
   strip replaced by IN/OUT stored at once. This needs no engine change: the cue already sets
   the marks.
2. **Entry editing.** Expanding entry rows, the join pills for the mix style. This is UI only,
   with `PUT` as today.
3. **Server.** Entry speed null, entry ids with `PATCH` of one entry, `copy_of`, clip copy and
   copy-for-entry, the same fields in the `playlist-editor` widget. Unit tests:
   - an entry that follows a clip speed edit;
   - id stability across `PUT`;
   - a `PATCH` while another `PUT` reorders;
   - a copy that is trimmed while the original stays;
   - protection after the original is deleted.
4. **Re-cue on edit.** §3.1 in the engine. Until then the screen re-cues a cued shot itself
   after an edit.

## 6a. Built so far

On the operator screen (`operator-ui/`), with the existing API plus `index` on `POST
/api/v1/playlists/{id}/cue|play`:

- the fader, the 200% setting, Save to clip/entry (§3.2);
- the speed badges with presets, All speeds (§3.3), the mix pills (§3.4);
- IN/OUT on the channel, stored in the loaded clip as they are marked, the timeline zoomed
  to the loaded clip (§3.5); an entry opens with its IN/OUT, end, auto-advance, Cue on A/B
  from it and Make a copy.

Not yet, because they need the server changes of phase 3:

- **"Clip" on an entry copies the clip's speed**; it does not follow later changes of the
  clip (`speed: null`). The badge is dashed with a link while the two are equal.
- **Entry ids** and `PATCH` of one entry: an entry edit reads the playlist, changes the
  entry and stores every entry, so two edits at the same moment can still overwrite each
  other.
- **Make a copy** is made from the channel (`POST /api/v1/clips` with its marks), so
  the entry has to be on a channel; there is no `copy_of` yet.
- The §3.1 re-cue: a cued shot keeps its marks after an edit until it is cued again.

## 7. Questions

Answered:

- A clip editor on the operator screen? **No**, too bulky. Speed works as on an EVS LSM
  (§3.3), and IN/OUT on the channel (§3.5).
- Should an entry's speed follow its clip's (`speed: null`)? **Yes** (§2, §3.4).
- Do entries need stable ids? **Probably yes** (§4). They are in phase 3.
- Fader range? **0–100 % by default**, with a setting for 0–200 % kept in the browser (§3.2).
- Keys for the speed? **None for now, mouse only** (§3.2).
- Mix length unit? **Frames** (§3.4).
- IN/OUT of entries? **Cue the clip or entry and set IN/OUT as for any shot**, then Update
  (stored at once, §3.5), and Make a copy first to change one entry. No special trim UI. This replaces the per-entry range of
  the first draft and the trim bar of the second.
- Copy naming? **The clip's name and the playlist in brackets** (§3.5).
- Deleting an original? **Its copies are kept** (§3.5).
- "Make a copy" for a clip used only by this entry? **Yes, always offered** (§3.5).
