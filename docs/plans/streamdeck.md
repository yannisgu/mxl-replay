# Stream Deck for clips and playlists (concept)

Status: concept, not built. It builds on the operators of `SPECIFICATION.md` §8.9, the shotbox and
playlists of §7 and §8.1, and the Behringer X-Touch One over Web MIDI (`web/src/midi.js`, branch
`feat/operators-multiview`). The EVS LSM remote was the starting point (Multicam LSM operating manual
11.00 and 12.05, chapters "Clip management" and "Playlist management"; links at the end), but most of
its clip addressing is not taken over (§2).

## 1. Who does what

The X-Touch One is the transport: the fader for the speed, the wheel for jog and shuttle, Play, Stop,
◀◀, ▶▶, and the marks. The Stream Deck does not repeat any of that. It is the **clip and playlist
surface**: store a clip, get it back, put it in a playlist, roll the playlist.

```
 ┌────────────────────────────┐   ┌──────────────────────┐
 │ Stream Deck                │   │ X-Touch One          │
 │ store, recall, pins,       │   │ fader = speed        │
 │ add to playlist, rundown,  │   │ wheel = jog/shuttle  │
 │ Next, Skip                 │   │ Play Stop ◀◀ ▶▶      │
 │                            │   │ marks                │
 └────────────────────────────┘   └──────────────────────┘
          and the screen: search, names, tags, editing
```

The flow it supports: mark IN and OUT (X-Touch), STORE (deck), cue the clip again a minute later
(deck), Play (X-Touch). During the game the operator presses ADD after each good clip to append it to
the highlights playlist. At half time they open the playlist and roll it.

## 2. Why no banks and no clip numbers

On the LSM, a clip has an address: clip 212 is page 2, bank 1, location 2, recalled with PAGE, SHIFT+F1
and F2. That design comes from the hardware: ten F keys with LEDs, no names, no pictures, and a small
LCD. The operator has to remember numbers because the remote cannot show anything else.

We have other means:

- **The screen shows everything.** The Shotbox tab has thumbnails, names, colours, tags, a filter and
  the countdowns. Finding a clip from an hour ago is a search there, not a bank number.
- **The keys are screens.** A key shows the clip's picture and name, so the operator does not need a
  number to know what is under it.
- **Live replay is almost always about the last few clips.** The clip stored a minute ago, rarely the
  one from 40 minutes ago.

So the deck does not mirror the whole library. It shows two short lists that matter under pressure:

- **Recent**: the clips just stored, filled automatically.
- **Pins**: a few clips the operator wants at a fixed key all show long (the opener, the sponsor
  billboard, the goal of the first half).

Everything else stays on the screen. This removes from the concept: fixed locations on the server,
banks, PAGE, storing "into" an empty key, and deleting from the deck.

What stays from the LSM, because it is about the workflow and not the hardware: store in one press,
ENTER to append to the current playlist, INSERT after the current entry, the playlist rundown with the
entry on air and the next one, and Next and Skip while it rolls.

## 3. Clip page (MK.2, 15 keys)

```
 ┌───────┬───────┬───────┬───────┬───────┐
 │ clip  │ clip  │ clip  │ clip  │ clip  │
 ├───────┼───────┼───────┼───────┼───────┤   Recent or Pins: 10 keys
 │ clip  │ clip  │ clip  │ clip  │ clip  │
 ├───────┼───────┼───────┼───────┼───────┤
 │ STORE │RECENT │  ADD  │ PLST  │ OLDER │
 │       │ /PINS │       │       │       │
 └───────┴───────┴───────┴───────┴───────┘
```

**Recent.** The last 10 clips stored by anyone, as a **ring**: a new clip takes the key of the oldest
one, and the others do not move. That matters: a list that shifts by one key each time a clip is
stored puts a different clip under the operator's finger just as they press. The newest clip has a
white corner badge, so the operator always finds it. A clip deleted in the UI leaves a dark key until
the next store fills it.

**Pins.** Up to 10 clips (or playlists) the operator put there. They stay until unpinned. To pin:

- hold a deck key for a second; it flashes "pick a clip"; click a clip on the screen and it goes on
  that key, or
- "Pin to deck" in a clip's menu in the UI, on the first free key.

Holding a pinned key and choosing "Unpin" on the screen removes it. Pins belong to the operator, not to
the browser (§7), so they are the same in the UI and on the deck.

**A clip key** shows the thumbnail, the name in the clip's colour and the duration; an angle group shows
the number of angles. A press is a **shotbox click** on the operator's primary channel
(`POST /api/v1/shotbox/{id}`): cue at IN, a second press plays, a press while it plays pauses. Most of
the time the operator cues on the deck and plays on the X-Touch, so the fader sets the speed.

**The function keys.**

- **STORE**: create a clip from the primary channel's IN and OUT (the `createClip` action the
  keyboard's C already has). The new clip lands in Recent at once. Without both marks the key flashes
  and nothing happens.
- **RECENT / PINS**: switch the ten keys between the two lists. The key shows which one is on.
- **ADD**: append to the **current playlist**, as ENTER on the LSM. A tap appends the newest clip (the
  common case: "that one was good, keep it"). Holding ADD and pressing a clip key appends that clip.
  The key shows the playlist's name and number of entries and flashes at each append. Without a current
  playlist it opens PLST.
- **PLST**: the playlist page (§4).
- **OLDER**: in Recent, the 10 clips before these (and again); NEWER comes back. In Pins, the next 10
  pins. This is the only paging left, and a long way back is the screen's job.

## 4. Playlist page and rundown

**PLST: the playlists.** The 10 keys show the playlists, most recently used first, the current one
lit. A key shows the stacked thumbnails (as the shotbox), the name, the number of entries and the total
duration. A press makes it the current playlist (the one ADD appends to) and opens its rundown. It does
not touch the channel. **NEW** makes an empty playlist named with the time ("Highlights 14:32") and
makes it current. The UI's Playlists tab marks the operator's current playlist too, so the screen and the deck agree.

**The rundown: one playlist's entries.**

```
 ┌───────┬───────┬───────┬───────┬───────┐
 │  1 ✓  │  2 ✓  │ 3 ON  │ 4 NXT │   5   │   entries, 10 at a time
 ├───────┼───────┼───────┼───────┼───────┤
 │   6   │   7   │   8   │   9   │  ▼ 11 │
 ├───────┼───────┼───────┼───────┼───────┤
 │ CLIPS │  CUE  │ NEXT  │ SKIP  │INSERT │
 └───────┴───────┴───────┴───────┴───────┘
```

- An entry key shows the clip's thumbnail and the entry's speed and transition ("75 % M 0:10" for a
  10-frame mix, as the LSM's playlist lines). The entry on air is red with the time left, the next one
  amber, the played ones dimmed with a tick.
- More than 9 entries: the last key scrolls (▼ 11: eleven more). The view follows the entry on air.
- A press on an entry **cues the playlist at that entry**, paused. Play on the X-Touch rolls from there.
- **CUE**: cue the playlist at its first entry. A second press plays.
- **NEXT**: while it rolls, go to the next entry now, with that entry's transition.
- **SKIP**: the next entry (amber) is not played this pass; the one after becomes next. The entry stays
  in the playlist. A second press takes the skip back.
- **INSERT**: the clip page comes up; the clip pressed there goes **after the selected entry** (the
  last one pressed, else the one on air), and the rundown comes back.
- Holding an entry for a second marks it red; a second press removes it from the playlist. The clip
  stays.
- **CLIPS**: back to the clip page.

Speeds, transitions, loop, the aux track and the end actions stay in the playlist editor of the UI. The
deck shows them and does not change them.

## 5. Colours and states

The UI's colours, so the deck and the screen agree.

| State | Key |
|---|---|
| clip | thumbnail and name |
| newest clip in Recent | white corner badge |
| cued on the operator's channel | amber frame (the shotbox's yellow) |
| on air on the operator's channel | red frame and the time left (`clip_remaining_ns`) |
| on air on another operator's channel | thin grey frame |
| function on (ADD held, INSERT, pick a clip) | the key filled: ADD green, INSERT amber, pick white |
| deck has lost the server | all keys dark except one "NO LINK" key |

A key reacts to the press at once (a short flash) before the server answers. A key is drawn again only
when what it shows changes; a thumbnail is fetched once per clip and kept.

## 6. Larger decks

The **XL** (32 keys, 8×4) shows clips and the rundown at once, which makes building a playlist during
the game faster:

```
 row 1      8 recent clips (ring)
 row 2      8 pins
 row 3      8 entries of the current playlist (scrolling)
 row 4     STORE  ADD  PLST  CUE  NEXT  SKIP  INSERT  OLDER
```

The **Stream Deck +**: a dial scrolls Recent back in time (instead of OLDER), another the rundown; the
touch strip shows the playlist on air with its time left. Later, if anyone asks.

## 7. What the server needs

The deck is a page module like `hid.js` and `midi.js` (`web/src/streamdeck.js`, WebHID with
`@elgato-stream-deck/webhid`, actions into `dispatch` of `App.vue`, on the operator's primary channel).
Already there: the shotbox click, create clip from the channel's marks (on the server),
`POST /api/v1/playlists/{id}/cue` and `…/play`, `PUT` of a playlist, the clips with their creation time
(Recent is just the newest clips), the status over `/api/v1/events`. Missing:

1. **Cue a playlist at an entry.** `…/cue` and `…/play` start at the first entry. Add `{"entry": n}`.
2. **Next and Skip.** `POST /api/v1/channels/{n}/playlist {"action": "next"}` starts the transition to
   the next entry now; `{"action": "skip", "entry": n}` and `"unskip"` leave an entry out this pass. The
   status gives the `entry` on air and the `next` one.
3. **Append and insert without a race.** Reading, changing and `PUT`ting the whole playlist loses an
   append when two operators add at the same moment. Add `POST /api/v1/playlists/{id}/entries
   {"clip", "after"}` and `DELETE …/entries/{n}`.
4. **Pins and the current playlist per operator.** `OP<n>_PINS` (10 clip or playlist ids, a comma list,
   empty slots allowed) and `OP<n>_PLAYLIST`, in `REPLAY_STATE_DIR/config.json` like `OP<n>_CONTROLLED`,
   set with `POST /api/v1/operators/{o}/pins` and `…/playlist`. A deleted clip leaves its pin empty. This
   could start in the browser's storage, but then a second screen or a new PC loses them.

Points 1–3 are also useful on the Playlists tab and the playlist widgets. The ring order of Recent is
computed in the page from the clips' creation times; it needs nothing on the server.

## 8. Steps

1. Deck module: connect, restore, Recent (ring), STORE, recall, OLDER. Test with an MK.2 in Chrome next
   to an X-Touch.
2. Server: pins and the current playlist per operator (4). Deck: PINS, pinning from the screen, ADD.
3. Server: cue at an entry (1), append and insert (3). Deck: PLST, the rundown, INSERT, remove.
4. Server: Next and Skip (2). Deck: NEXT and SKIP while rolling.
5. The XL layout and a Settings section (connect, brightness, which page the deck opens on).
6. Later: the Stream Deck +; a Companion module for trucks where the deck is shared with other systems.

## 9. Open questions

- **Recent for everyone or per operator?** With two operators, should Recent show all new clips or only
  the ones this operator stored? Per operator keeps it calm; for all lets one operator roll the other's
  clip at once.
- **Play on the deck at all?** If the operators always play from the X-Touch, a second press on a cued
  clip could do nothing instead of playing. That avoids an accidental play.
- **Angles.** On the LSM a second press on a recalled clip steps through its angles. Here it plays.
  Angles could go on X-Touch buttons instead.
- **Ten pins enough?** Or do some shows need a full page of pins (the XL's 16 or 24 keys), which brings
  a small kind of bank back?

## Sources

- EVS Multicam LSM 11.00 operating manual: clip addressing and storing (viewer pages 48–51), LEDs (17),
  playlist edit and playout (98, 107), append and insert (111–112):
  https://www.manualsdir.com/manuals/254262/evs-multicamlsm-version-1100-operating-manual.html
- EVS Multicam LSM 12.05 operation manual, playlist management (viewer pages 90–98):
  https://manualslib.com/manual/1224276/Evs-Multicam-Lsm.html?page=98
