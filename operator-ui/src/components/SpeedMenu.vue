<script setup lang="ts">
// The speed presets of a clip, a playlist entry or every entry of a playlist (an LSM's "edit all"), opened from a
// speed badge. Other speeds are set as on an LSM: play it, move the fader, Save.
import { computed, nextTick, onMounted, onUnmounted, ref, watch } from "vue";
import { fmtSpeed } from "../lib/api";
import { clipsById, setAllSpeeds, setClipSpeed, setEntry, state } from "../lib/store";
import Icon from "./Icon.vue";

const menu = computed(() => state.speedMenu);
const presets = computed(() => [0.25, 0.33, 0.5, 0.75, 1, ...(state.faderMax > 1 ? [1.5, 2] : [])]);

const entry = computed(() => {
  const m = menu.value;
  return m?.kind === "entry" && state.playlist?.id === m.id ? state.playlist.entries[m.index] || null : null;
});
const entryClip = computed(() => (entry.value ? clipsById.value[entry.value.clip_id] || null : null));
const current = computed(() => {
  const m = menu.value;
  if (!m) return null;
  if (m.kind === "clip") return clipsById.value[m.id]?.speed ?? null;
  if (m.kind === "entry") return entry.value?.speed ?? null;
  return null;
});
const title = computed(() => {
  const m = menu.value;
  if (!m) return "";
  if (m.kind === "clip") return `Speed of ${clipsById.value[m.id]?.name || "the clip"}`;
  if (m.kind === "entry") return `Speed of entry ${m.index + 1}`;
  return "Speed of every entry";
});
const near = (a: number | null, b: number) => a != null && Math.abs(a - b) < 0.005;

function pick(v: number | null) {
  const m = menu.value;
  const clipSpeed = entryClip.value?.speed; // read before the menu closes
  state.speedMenu = null;
  if (!m) return;
  if (m.kind === "clip" && v != null) setClipSpeed(m.id, v);
  else if (m.kind === "entry") setEntry(m.id, m.index, { speed: v ?? clipSpeed ?? 1 });
  else if (m.kind === "all") setAllSpeeds(m.id, v);
}

// Placed under the badge (above it near the bottom), inside the window.
const el = ref<HTMLElement | null>(null);
const pos = ref({ left: "0px", top: "0px" });
watch(
  menu,
  async (m) => {
    if (!m) return;
    await nextTick();
    const w = el.value?.offsetWidth || 240;
    const h = el.value?.offsetHeight || 100;
    pos.value = {
      left: `${Math.max(8, Math.min(innerWidth - w - 8, m.x - w + 24))}px`,
      top: `${m.y + 28 + h > innerHeight ? m.y - h - 6 : m.y + 24}px`,
    };
  },
  { immediate: true },
);
function outside(e: PointerEvent) {
  const t = e.target as HTMLElement;
  if (menu.value && !el.value?.contains(t) && !t.closest("[data-speed-badge]")) state.speedMenu = null;
}
onMounted(() => document.addEventListener("pointerdown", outside, true));
onUnmounted(() => document.removeEventListener("pointerdown", outside, true));
</script>

<template>
  <div v-if="menu" ref="el" class="spmenu" :style="pos" role="dialog" :aria-label="title">
    <span class="t">{{ title }}</span>
    <button v-if="menu.kind === 'entry'" class="b" :class="{ on: entryClip && near(current, entryClip.speed) }" title="The clip's speed" @click="pick(null)">
      <Icon name="link" />Clip {{ entryClip ? fmtSpeed(entryClip.speed) : "" }}
    </button>
    <button v-if="menu.kind === 'all'" class="b" title="Each entry at its clip's speed" @click="pick(null)"><Icon name="link" />Clip</button>
    <button v-for="v in presets" :key="v" class="b" :class="{ on: near(current, v) }" @click="pick(v)">{{ Math.round(v * 100) }}</button>
    <span class="t sm">Other speeds: play it, move the fader, Save.</span>
  </div>
</template>

<style scoped>
.spmenu { position: fixed; z-index: 30; display: flex; flex-wrap: wrap; gap: 4px; max-width: 290px; padding: 8px; background: var(--panel2);
  border: 1px solid var(--border2); border-radius: 6px; box-shadow: 0 8px 24px rgba(0,0,0,.5); }
.t { flex-basis: 100%; font-size: 12px; font-weight: 600; padding: 0 2px 2px; }
.t.sm { font-weight: 400; font-size: 11px; color: var(--muted); padding: 2px 2px 0; }
.b { height: 30px; min-width: 0; padding: 0 9px; }
</style>
