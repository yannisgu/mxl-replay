# Concept: tagging clips on the operator screen

Status: built, changed after review (§7): no ratings, no groups, tagging in a selected-clip panel. Once agreed, it becomes part of
SPECIFICATION.md §7 (clips) and §8.2a (the operator screen).

## 1. Why

A clip has `tags`, a free comma-separated text (§7). Today:

- The main UI sets tags in the *Create clip* dialog (typed: "goal, home team") and in the
  clip inspector.
- The operator screen cannot set tags at all. Its clips are made by OUT (IN, OUT, Space) and
  get none.
- The bin's filter chips are the 8 tags used most. Typed tags drift ("Goal", "goal",
  "goals"), so each spelling becomes its own chip.

During a match an operator has seconds between two actions. Tagging has to be one or two
clicks on words that already exist, not typing. The tags then make the bin searchable
(every goal, every save of the keeper) and are the raw material for highlight playlists.

## 2. What an EVS LSM does

From the Multicam LSM operating manuals and the LSM-VIA user manual. Key names and screens
differ between versions.

| EVS | What it does |
| --- | --- |
| Keyword file | A list of keywords prepared before the show, chosen in the setup; assigning and searching use the current file |
| 3 keywords + ranking per clip | A clip holds at most 3 keywords and a ranking of –, \*, \*\*, \*\*\* |
| On-air / off-air keyword screens | A grid of the file's keywords; a click (or cursor and ENTER) assigns one to the loaded clip |
| Before the clip exists | Keywords and ranking can be assigned before the clip is created, and the clip gets them |
| ALT + DEL | Clears a clip's keywords and resets its ranking |
| OSD | With *Keyword Info* on, the loaded clip's keywords and ranking show on the output monitor until it moves |
| LSM-VIA | Keywords and rating in the touchscreen's Metadata window; only on the clip angle loaded on the first PGM |

So the EVS model is a fixed vocabulary, a grid to click, a small number of keywords per
clip, a rating, and assigning before the clip exists.

## 3. Proposal

### 3.1 A tag set per show

The replay keeps a **tag set**: the words that can be given to clips, in groups.

| Group (example) | Tags (example) |
| --- | --- |
| Action | Goal, Chance, Save, Foul, Card, Offside, Corner, Free kick |
| Team | Home, Away |
| Players | #7 Müller, #10 Rossi, … |
| Other | Crowd, Coach, Replay of the day |

- It is stored on the server (state directory, config export), so every operator screen and
  the main UI use the same words.
- It is edited in the main UI (Settings or the Library), before the show: add, rename,
  reorder, groups. A rename changes the tag on every clip that has it.
- Clips keep `tags` as today (a comma list of the set's names), so the API, the catalog and
  older clips stay compatible. A tag a clip has that is not in the set still shows; it is
  just not on the palette.

### 3.2 The tag palette

The operator screen gets a **palette**: the tag set's buttons by group, in a compact strip.
It acts on **the clip under control**: the clip loaded on the channel under control (cued,
playing or paused in it).

- A click toggles the tag on that clip (`PATCH /api/v1/clips/{id} {"tags"}`). The buttons of
  tags the clip has are lit.
- **No clip loaded: the tags are armed.** The clicked buttons stay lit in another colour
  ("armed"), and the next clip OUT makes gets them, then the palette clears. This is the EVS
  "before the clip exists": tag while the action is still running, then IN, OUT.
- No limit of 3. The palette shows what is lit, which is enough to keep it short.

Where it sits (to decide with a prototype, §5):

- **A.** A row under the channels, above the cameras: always visible, costs about 40 px.
- **B.** A *Tags* button in the channel strip that opens the palette as a popover over the
  monitor area: no space, one more click.
- **C.** A tab in the sidebar next to the playlist: space for many tags, but it hides the
  playlist.

### 3.3 Rating

A rating of 0–3 stars, as on an EVS, is a separate field (`rating`), not a tag: one click on
the stars next to the palette, and the bin can sort or filter by it ("★★ and up"). Optional;
see §6.

### 3.4 In the bin

- The filter chips become the tag set's groups and tags (not the 8 used most). Several
  chips filter with AND: *Goal* + *Home*.
- Each clip card shows its tags as small coloured chips under the name (the group's
  colour), and its stars.
- The ⋯ menu gets *Tags…*: the palette for that clip, without loading it.
- Later: *New playlist from filter*: the clips shown, in time order.

### 3.5 Names

A clip made by OUT is named "clip-12" today. With tags it could be named from them: the
first action tag and the time, "Goal 12:04", renamed when its first tag changes until
someone renames it by hand. See §6.

## 4. API

| Change | Where |
| --- | --- |
| Tag set | `GET` / `PUT /api/v1/tags`: `{"groups":[{"name","colour","tags":[…]}]}`; in the state directory and the config export |
| Rename a tag | `PUT` with `{"rename":{"from","to"}}`, changes every clip's `tags` |
| Clip tags | `PATCH /api/v1/clips/{id} {"tags"}` (exists) |
| Tags at creation | `POST /api/v1/clips {"tags"}` (exists): the operator screen sends the armed tags with OUT |
| Rating | `rating` 0–3 on a clip, in `PATCH`, the answers, the catalog and the export |

## 5. Phases

1. **Palette on the operator screen**, with a fixed tag set in the screen itself (no server
   change): toggle on the clip under control, arm before OUT, tags on clip cards, ⋯ *Tags…*,
   filters by tag. Try placements A and B on mxl-test.
2. **Tag set on the server** and its editor in the main UI; the palette reads it.
3. Rating, names from tags, *New playlist from filter*, tags on the OSD of a loaded clip.

## 6. Decided

- **No ratings** for now (§3.3 is not built).
- **Shared tag list** on the server (§3.1, §4), with a default list until one is stored.

## 7. Built (after review)

The review changed the proposal:

- **No groups**: the tag list is flat. `GET /api/v1/tags` gives every tag of the list or of a
  clip with its number of clips, most used first; `POST` adds one, `PUT` replaces the list or
  renames a tag on every clip.
- **No tag bar and no list editor.** Tagging is in a panel at the bottom of the bin, for the
  **selected** clip (not the cued one; a clip OUT makes is selected): rename it, × removes a
  tag, the 8 most used tags are one click (+), and a box finds any tag as it is typed or
  creates a new one (Enter), which joins the shared list.
- No arming before the clip exists: the new clip is selected, so it is tagged right after
  OUT.
- The bin's filter chips are the 8 most used tags (AND); other tags through the search.
- Not yet: names from tags, *New playlist from filter*, tags on the OSD, a way to delete a
  tag from the list in the UI (the API can: `PUT` the list without it).

## 8. Open questions

- **Placement** of the palette: A, B or C (§3.2)?
- **Groups**: are Action, Team, Players the right ones? Should a player tag imply the team?
- **Rating**: wanted, or are tags enough?
- **Names from tags** ("Goal 12:04"), or keep the ids and rely on the tags?
- **Keys**: tags by number keys or a Stream Deck page (docs/plans/streamdeck.md), or mouse
  only like the speed?
- **All angles**: when a clip has copies on other angles (*all angles*, the group), should a
  tag go to every angle? On LSM-VIA it goes to the angle on the first PGM only.
