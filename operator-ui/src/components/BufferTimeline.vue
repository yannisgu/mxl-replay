<script setup lang="ts">
// The recent buffer under a channel: its position, the IN–OUT range and the clips, from 20 minutes ago (more when the
// position or IN is older, at most the whole buffer) to live. With a clip or playlist entry loaded it zooms to the
// clip and 10 s around it, the clip's stored IN/OUT dashed under the marks, so IN and OUT can be set frame by frame.
// Click or drag seeks; the wheel steps frames (Shift: 1 s).
import { computed, ref } from "vue";
import type { Channel } from "../lib/api";
import { smoothPos } from "../lib/smooth";
import { loadedClip, newestNs, oldestNs, scrubFrames, scrubSeconds, seek, state } from "../lib/store";

const props = defineProps<{ ch: Channel }>();
const MIN = 60e9;

const PAD = 10e9;
const loaded = computed(() => loadedClip(props.ch));
/** The zoomed window around the loaded clip and the marks, or null. */
const zoom = computed(() => {
  const c = loaded.value;
  if (!c) return null;
  const ch = props.ch;
  const lo = Math.min(c.in_ns, ch.has_in ? ch.in_ns : c.in_ns) - PAD;
  const hi = Math.min(newestNs.value, Math.max(c.out_ns, ch.has_out ? ch.out_ns : c.out_ns) + PAD);
  return hi > lo ? { lo, hi } : null;
});

const recentSpan = computed(() => {
  const newest = newestNs.value;
  const furthest = Math.min(props.ch.position_ns || newest, props.ch.has_in ? props.ch.in_ns : newest);
  let minutes = 20;
  while (newest - furthest > (minutes - 1) * MIN && minutes < 24 * 60) minutes *= 2;
  return Math.max(MIN, Math.min(minutes * MIN, newest - oldestNs.value || minutes * MIN));
});
const span = computed(() => (zoom.value ? zoom.value.hi - zoom.value.lo : recentSpan.value));
const start = computed(() => (zoom.value ? zoom.value.lo : newestNs.value - span.value));
const stored = computed(() => {
  const c = loaded.value;
  if (!c || !zoom.value) return null;
  const l = pct(c.in_ns);
  return { left: `${l}%`, width: `${Math.max(0.2, pct(c.out_ns) - l)}%` };
});
const pct = (t: number) => Math.max(0, Math.min(100, (100 * (t - start.value)) / span.value));

const range = computed(() => {
  const c = props.ch;
  if (!c.has_in) return null;
  const end = c.has_out && c.out_ns > c.in_ns ? c.out_ns : c.position_ns;
  const l = pct(c.in_ns);
  return { left: `${l}%`, width: `${Math.max(0.2, pct(end) - l)}%` };
});

const clipMarks = computed(() =>
  state.clips
    .filter((c) => c.camera !== 0 && c.out_ns > start.value)
    .map((c) => {
      const l = pct(c.in_ns);
      return { id: c.id, title: c.name, colour: c.colour, left: `${l}%`, width: `max(3px, ${pct(c.out_ns) - l}%)` };
    }),
);

const ticks = computed(() => {
  if (zoom.value && loaded.value) {
    // Seconds from the clip's IN.
    const secs = span.value / 1e9;
    const step = [1, 2, 5, 10, 30, 60].find((s) => secs / s <= 8) || 120;
    const inNs = loaded.value.in_ns;
    const out = [];
    for (let s = Math.ceil((start.value - inNs) / 1e9 / step) * step; inNs + s * 1e9 <= start.value + span.value; s += step) {
      const at = pct(inNs + s * 1e9);
      if (at > 3 && at < 97) out.push({ left: `${at}%`, text: s ? `${s > 0 ? "+" : "−"}${Math.abs(s)}s` : "IN" });
    }
    return out;
  }
  const minutes = span.value / MIN;
  const step = [1, 2, 5, 10, 15, 30, 60, 120, 240].find((s) => minutes / s <= 5) || 480;
  const out = [];
  for (let m = 0; m <= minutes; m += step) out.push({ left: `${100 - (100 * m) / minutes}%`, text: m ? (m >= 60 && m % 60 === 0 ? `−${m / 60}h` : `−${m}′`) : "" });
  return out;
});

const el = ref<HTMLElement | null>(null);
let dragging = false;
function seekAt(e: PointerEvent) {
  const r = el.value!.getBoundingClientRect();
  const f = Math.min(1, Math.max(0, (e.clientX - r.left) / r.width));
  seek(props.ch.index, Math.min(newestNs.value, start.value + f * span.value));
}
function down(e: PointerEvent) {
  dragging = true;
  el.value!.setPointerCapture(e.pointerId);
  seekAt(e);
}
const move = (e: PointerEvent) => dragging && seekAt(e);
const up = () => (dragging = false);
function wheel(e: WheelEvent) {
  const d = e.deltaY || e.deltaX;
  if (!d) return;
  if (e.shiftKey) scrubSeconds(props.ch.index, d > 0 ? 1 : -1);
  else scrubFrames(props.ch.index, d > 0 ? 1 : -1);
}
</script>

<template>
  <div ref="el" class="tl" title="Click or drag to seek · wheel: frames (Shift: seconds)" @pointerdown="down" @pointermove="move" @pointerup="up" @pointercancel="up" @wheel.prevent="wheel">
    <div v-for="m in clipMarks" :key="m.id" class="clipm" :title="m.title" :style="{ left: m.left, width: m.width, background: m.colour }"></div>
    <div v-if="stored" class="stored" :title="`${loaded?.name}: IN/OUT as stored`" :style="stored"></div>
    <div v-if="range" class="range" :style="range"></div>
    <div class="head" :style="{ left: `${pct(smoothPos(ch))}%` }"></div>
    <div class="ticks"><span v-for="t in ticks" :key="t.left" :style="{ left: t.left }">{{ t.text }}</span></div>
  </div>
</template>

<style scoped>
.tl { position: relative; height: 36px; background: var(--sunk); border: 1px solid var(--border); border-radius: 5px; cursor: ew-resize; overflow: hidden; touch-action: none; }
.clipm { position: absolute; top: 0; height: 6px; border-radius: 0 0 2px 2px; opacity: .8; }
.range { position: absolute; top: 8px; bottom: 14px; background: var(--accent-tint); border: 1px solid var(--accent); border-radius: 2px; }
.stored { position: absolute; top: 5px; bottom: 11px; border: 1px dashed var(--muted); border-radius: 2px; pointer-events: none; }
.tl:has(.stored) .ticks span:last-child { transform: translateX(-50%); }
.head { position: absolute; top: 0; bottom: 0; width: 2px; margin-left: -1px; background: #fff; }
.ticks { position: absolute; inset: auto 0 0 0; height: 13px; font-size: 11px; color: var(--muted); font-family: var(--mono); pointer-events: none; }
.ticks span { position: absolute; bottom: 0; transform: translateX(-50%); }
.ticks span:last-child { transform: none; }
</style>
