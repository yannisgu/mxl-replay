<script setup lang="ts">
// The speed fader, mouse only: drag (snaps to the presets within ±2 %; Alt: no snapping) or the wheel (±1 %,
// Shift ±10 %). 0–100 %, or 0–200 % with `max` 2, where 0–100 % keeps two thirds of the travel.
// `input` while it moves; the parent throttles what it sends.
import { computed, ref } from "vue";

const props = withDefaults(defineProps<{ value: number; max?: number; dim?: boolean }>(), { max: 1, dim: false });
const emit = defineEmits<{ input: [speed: number] }>();

const TICKS = computed(() => [0.25, 0.33, 0.5, 0.75, 1, ...(props.max > 1 ? [1.5, 2] : [])]);
const toX = (v: number) => (props.max <= 1 ? v : v <= 1 ? (v * 2) / 3 : 2 / 3 + (v - 1) / 3);
const fromX = (x: number) => (props.max <= 1 ? x : x <= 2 / 3 ? (x * 3) / 2 : 1 + (x - 2 / 3) * 3);
const at = computed(() => `${toX(Math.min(props.max, Math.max(0, props.value))) * 100}%`);

function snap(v: number, free: boolean) {
  if (!free) for (const t of TICKS.value) if (Math.abs(v - t) <= 0.02) return t;
  return Math.round(v * 100) / 100;
}

const el = ref<HTMLElement | null>(null);
const dragging = ref(false);
function setAt(e: PointerEvent) {
  const r = el.value!.getBoundingClientRect();
  const x = Math.min(1, Math.max(0, (e.clientX - r.left) / r.width));
  emit("input", snap(Math.min(props.max, fromX(x)), e.altKey));
}
function down(e: PointerEvent) {
  if (e.button !== 0) return;
  dragging.value = true;
  el.value!.setPointerCapture(e.pointerId);
  setAt(e);
}
const move = (e: PointerEvent) => dragging.value && setAt(e);
const up = () => (dragging.value = false);
function wheel(e: WheelEvent) {
  const d = e.deltaY || e.deltaX;
  if (!d) return;
  const step = (e.shiftKey ? 0.1 : 0.01) * (d < 0 ? 1 : -1);
  emit("input", Math.round(Math.min(props.max, Math.max(0, props.value + step)) * 100) / 100);
}
</script>

<template>
  <div
    ref="el"
    class="fader"
    :class="{ dim, drag: dragging }"
    role="slider"
    aria-label="Speed"
    :aria-valuenow="Math.round(value * 100)"
    aria-valuemin="0"
    :aria-valuemax="max * 100"
    title="Speed: drag · wheel ±1 % (Shift ±10 %) · Alt: no snapping"
    @pointerdown="down"
    @pointermove="move"
    @pointerup="up"
    @pointercancel="up"
    @wheel.prevent="wheel"
  >
    <div class="track"></div>
    <div class="fill" :style="{ width: at }"></div>
    <div v-for="t in TICKS" :key="t" class="tick" :class="{ one: t === 1 }" :style="{ left: `${toX(t) * 100}%` }"><span>{{ Math.round(t * 100) }}</span></div>
    <div class="thumb" :style="{ left: at }"></div>
  </div>
</template>

<style scoped>
.fader { position: relative; flex: 1; min-width: 140px; height: 30px; cursor: ew-resize; touch-action: none; user-select: none; }
.track { position: absolute; left: 0; right: 0; top: 7px; height: 6px; border-radius: 3px; background: var(--sunk); border: 1px solid var(--border2); }
.fill { position: absolute; left: 0; top: 7px; height: 6px; border-radius: 3px; background: var(--accent); opacity: .85; }
.tick { position: absolute; top: 16px; width: 1px; height: 4px; background: var(--border2); }
.tick.one { background: var(--muted); height: 6px; }
.tick span { position: absolute; top: 4px; left: 50%; transform: translateX(-50%); font: 600 10px var(--mono); color: var(--muted); white-space: nowrap; }
.thumb { position: absolute; top: 0; width: 14px; height: 20px; margin-left: -7px; border-radius: 4px; background: var(--text); border: 2px solid var(--bg); box-shadow: 0 0 0 1px var(--border2); }
.fader:hover .thumb, .fader.drag .thumb { background: #fff; box-shadow: 0 0 0 2px var(--accent); }
.fader.dim .fill { background: var(--muted); opacity: .5; }
.fader.dim .thumb { background: var(--muted); }
</style>
