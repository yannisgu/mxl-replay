<script setup lang="ts">
defineEmits<{ close: [] }>();
</script>

<template>
  <div class="keys" role="dialog" aria-label="Keys" @click="$emit('close')">
    <div class="card">
      <h3>Keys</h3>
      <div><kbd>A</kbd><kbd>B</kbd> take control of a channel</div>
      <div><kbd>Space</kbd> play / pause</div>
      <div><kbd>I</kbd><kbd>O</kbd> mark IN / OUT (OUT makes the clip and cues it)</div>
      <div><kbd>Tab</kbd> next channel</div>
      <div><kbd>L</kbd> back to live (E2E)</div>
      <div><kbd>1–9</kbd> camera angle</div>
      <div><kbd>← →</kbd> one frame (Shift: 1 s)</div>
      <div><kbd>Enter</kbd> cue selected clip</div>
      <div><kbd>F</kbd> full screen</div>
      <div><kbd>[ ]</kbd> go to IN / OUT</div>
      <div><kbd>P</kbd> add clip to playlist</div>
      <h3 class="sub">X-Touch One (Mackie Control mode, button X-Touch)</h3>
      <div><kbd>Fader</kbd> speed (motor follows)</div>
      <div><kbd>Wheel</kbd> 1 frame, 5 frames, 1 s</div>
      <div><kbd>Scrub</kbd> wheel step</div>
      <div><kbd>Arrows</kbd> select a clip</div>
      <div><kbd>Middle</kbd> cue the clip</div>
      <div><kbd>Play</kbd> play / pause · <kbd>Stop</kbd> pause</div>
      <div><kbd>Record</kbd> live</div>
      <div><kbd>◀◀ ▶▶</kbd> spool, again: 2× 5× 10×</div>
      <div><kbd>Marker</kbd><kbd>Nudge</kbd> IN / OUT</div>
      <div><kbd>Bank ◀▶</kbd> camera</div>
      <div><kbd>Channel ◀▶</kbd> control A / B</div>
      <div><kbd>F1–F6</kbd> camera 1–6, top REC held: 7–12</div>
      <p>Keys, angle tiles and the clip bin act on the channel under control (blue). Red is on air.</p>
    </div>
  </div>
</template>

<style scoped>
.keys { position: fixed; inset: 0; padding: 16px; overflow-y: auto; background: rgba(5,8,12,.82); display: grid; place-items: center; z-index: 10; }
.card { background: var(--panel); border: 1px solid var(--border2); border-radius: 10px; padding: 24px 30px; display: grid; grid-template-columns: repeat(3, auto); gap: 6px 48px; font-size: 16px; }
@media (max-width: 1000px) { .card { grid-template-columns: repeat(2, auto); gap: 6px 28px; } }
@media (max-width: 600px) { .card { grid-template-columns: auto; padding: 18px; font-size: 14px; } }
h3 { grid-column: 1 / -1; margin: 0 0 10px; font-size: 18px; }
h3.sub { margin: 14px 0 4px; font-size: 15px; color: var(--muted); }
.card div { display: flex; gap: 12px; align-items: center; }
kbd { font: 700 14px var(--mono); min-width: 52px; text-align: center; padding: 3px 7px; border: 1px solid var(--border2); border-radius: 5px; background: var(--chip); box-shadow: 0 1px 0 rgba(0,0,0,.5); }
p { grid-column: 1 / -1; margin: 12px 0 0; color: var(--muted); font-size: 14px; }
</style>
