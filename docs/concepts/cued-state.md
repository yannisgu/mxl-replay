# Concept: making a cued clip obvious

Status: 3.1, 3.3 and 3.4 built; 3.2 built, then replaced after review by a small OSD line (§6). The operator screen is in SPECIFICATION.md §8.2a.

## 1. The problem

With a clip loaded, the keys mean something else: IN and OUT trim the clip, a camera changes
its angle, Space plays it. With nothing loaded, OUT makes a new clip. An operator who does not
see at a glance which of the two is true will trim a clip they meant to leave alone, or make a
clip they meant to trim.

Today a cued clip shows like this, all of it small or away from the picture:

| Where | What |
| --- | --- |
| Channel header | The clip's name in grey after the channel name, an *Uncue* button |
| Monitor | The countdown in the corner (−7.3 s); otherwise a still picture, like a paused channel |
| Timeline | Zoomed to the clip, its stored range dashed |
| Clip bin | A yellow border and "A CUED" on the clip's card |
| Playlist | A yellow border and "A CUED" on the entry |
| Cameras | The channel's letter on the clip's camera tile |

The monitor, where the operator looks, says least. Cued, paused and scrubbed look the same.

## 2. What an EVS LSM does

From the Multicam LSM operating manuals and the LSM-VIA user manual (versions differ):

- **On the monitor's OSD.** The play channel's name is on the OSD of the monitoring output,
  and the controlled channel is marked with stars, on the OSD and on the remote's LCD:
  `*PGM1*`.
- **Clip information at the short IN.** With *Keyword Info* on, a loaded clip's keywords and
  ranking show on the monitoring OSD while it sits at its short IN point. As soon as the
  operator jogs into it or plays it, the information goes, so the picture is clear.
- **The remote's LCD** shows the operation menus, with the same message on the output OSD.

So an EVS puts the state **on the monitor**, says it in text, and takes it away once the
operator works in the picture. The OSD is the primary place; the panels repeat it.

## 3. Ideas

Each can be combined with the others. Sketches are of one channel.

### 3.1 State colour on the monitor's inner frame

A 4 px frame **inside** the monitor in the state's colour: yellow cued, green playing a clip,
none live or scrubbing. The tally stays the outer border (red), so a cued clip on air is red
outside and yellow inside. One look from across the desk tells the state.

```
┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓  ← tally (red when on air)
┃┌──────────────────────────────────┐┃
┃│▓ yellow inner frame: cued        │┃
┃│                                  │┃
┃└──────────────────────────────────┘┃
```

### 3.2 A slate at IN, gone once the picture moves (the EVS way)

While the clip sits cued at its IN, the lower third of the monitor shows a **slate**: the
name large, tags, length and speed, the camera, and "Space plays". The first jog, play or mark
fades it out, as EVS hides its keyword info. Back at IN, still cued: it comes back.

```
│                                    │
│ ┌────────────────────────────────┐ │
│ │ CUED  Great save               │ │
│ │ Save · Away   4.2 s at 50 %  2 │ │
│ │ Space plays · I/O trim it       │ │
│ └────────────────────────────────┘ │
│ 15:28:06:12                   50 % │
```

### 3.3 A mode word in the header

The header says the mode in a fixed place, as a coloured word: `LIVE`, `CUED`, `PLAY`,
`PAUSED`, `FREE` (scrubbed, no clip), `END`. The clip name follows in white. *Uncue* sits next
to `CUED` and `PAUSED` (a clip loaded), so it is where the operator looks for the state.

```
[A] LSM A  ▌CUED▐ Great save · 4.2 s                    ⏏ Uncue
```

### 3.4 What the keys do, said where they are

With a clip loaded the strip's buttons change their words: IN becomes **Trim IN**, OUT becomes
**Trim OUT**, and the camera tiles say **Clip angle**. Without one, OUT says **OUT · new clip**.
The keys stay the same; the labels say what they will do now. This is the one idea that
answers the problem directly: what will this key do?

### 3.5 The clip's colour on the channel

A cued clip takes over the channel's accent: the timeline's range, the countdown's background
and the header's mode word in the clip's swatch colour. Clips with tag colours or team colours
then show which clip it is without reading. Risk: it fights the state colours (yellow cued,
green playing), so it would be the range and a small swatch only.

### 3.6 A short animation on cue

The slate (3.2) slides in, or the inner frame (3.1) pulses once, when a clip is cued, so a cue
from another place (the bin, a playlist, a Stream Deck) is noticed. Once only; nothing
blinks on air.

### 3.7 Elsewhere on the screen

- Clip bin and playlist: the cued card filled in yellow (not only the border), so it is found
  in a long list; *scroll to it* when it is cued from elsewhere.
- Camera tiles: the clip's camera tile with a yellow corner "CLIP".
- Stream Deck (docs/plans/streamdeck.md): the IN and OUT keys in yellow while a clip is loaded.

## 4. Recommendation

1. **3.3 mode word** and **3.4 labels**: cheap, and they answer "what will the key do". Build
   first.
2. **3.1 inner frame**: the state from across the room, and it keeps the tally apart.
3. **3.2 slate**: the EVS habit; worth trying on mxl-test with real pictures to see how much it
   covers.
4. 3.5–3.7 after the operators have used 1–3.

A prototype page with 3.1–3.4 on the existing operator screen (docs/prototypes) would let us
compare them before building.

## 5. Questions

- Is yellow right for cued on the monitor, next to the red tally and the yellow speed
  countdown under 5 s?
- Should the slate show at all on a channel that is on air (red tally)? EVS shows it on the
  monitoring output, which the audience does not see; ours is the same, but the operator's
  eye is on that monitor during a take.
- Labels changing (3.4): helpful, or unsettling when the keys move under the finger?

## 6. Built, and changed after review

- 3.3 mode word, 3.4 labels (Trim IN / Trim OUT, OUT · new clip, "Angle of …" on the
  cameras) and 3.1 inner frame are on the operator screen.
- 3.2 slate was too much on the picture. In its place, at the bottom of the monitor, small
  and half transparent: the clip's name, `IN +1.00 s` (time from IN) and `OUT −4.80 s` (time
  to OUT, `+` once past it, red in the last 5 s while it plays). The countdown box in the
  corner is gone.
- The camera label is plain text with a shadow, no box. The big timecode moved off the
  monitor into the strip, under the transport buttons.
