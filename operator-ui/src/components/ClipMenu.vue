<script setup lang="ts">
// A clip's ⋯ menu: Duplicate, Delete (a second click confirms; it says which playlists and channels have the clip).
import { computed, nextTick, onMounted, onUnmounted, ref, watch } from "vue";
import { clipUse, clipsById, deleteClip, duplicateClip, letter, state } from "../lib/store";
import Icon from "./Icon.vue";

const menu = computed(() => state.clipMenu);
const clip = computed(() => (menu.value ? clipsById.value[menu.value.id] || null : null));
const use = computed(() => (menu.value ? clipUse(menu.value.id) : { playlists: [], channels: [] }));
const confirming = ref(false);
watch(menu, () => (confirming.value = false));

function duplicate() {
  const id = menu.value?.id;
  state.clipMenu = null;
  if (id) duplicateClip(id);
}
function remove() {
  if (!confirming.value) {
    confirming.value = true;
    return;
  }
  const id = menu.value?.id;
  state.clipMenu = null;
  if (id) deleteClip(id);
}

// Placed under the button (above it near the bottom), inside the window.
const el = ref<HTMLElement | null>(null);
const pos = ref({ left: "0px", top: "0px" });
watch(
  [menu, confirming],
  async ([m]) => {
    if (!m) return;
    await nextTick();
    const w = el.value?.offsetWidth || 220;
    const h = el.value?.offsetHeight || 90;
    pos.value = {
      left: `${Math.max(8, Math.min(innerWidth - w - 8, m.x - w + 24))}px`,
      top: `${m.y + 28 + h > innerHeight ? m.y - h - 6 : m.y + 24}px`,
    };
  },
  { immediate: true },
);
function outside(e: PointerEvent) {
  const t = e.target as HTMLElement;
  if (menu.value && !el.value?.contains(t) && !t.closest("[data-clip-menu]")) state.clipMenu = null;
}
onMounted(() => document.addEventListener("pointerdown", outside, true));
onUnmounted(() => document.removeEventListener("pointerdown", outside, true));
</script>

<template>
  <div v-if="menu && clip" ref="el" class="cmenu" :style="pos" role="menu" :aria-label="clip.name">
    <span class="t">{{ clip.name }}</span>
    <button class="item" role="menuitem" @click="duplicate"><Icon name="plus" />Duplicate</button>
    <button class="item danger" :class="{ sure: confirming }" role="menuitem" @click="remove">
      <Icon name="x" />{{ confirming ? "Delete for good" : "Delete" }}
    </button>
    <span v-if="confirming" class="warn">
      <template v-if="use.playlists.length">In {{ use.playlists.join(", ") }}: those entries are skipped. </template>
      <template v-if="use.channels.length">Loaded on {{ use.channels.map((c) => letter(c)).join(", ") }}. </template>
      <template v-if="!use.playlists.length && !use.channels.length">Not in a playlist. </template>
      Click again to delete.
    </span>
  </div>
</template>

<style scoped>
.cmenu { position: fixed; z-index: 30; display: flex; flex-direction: column; gap: 2px; width: 230px; padding: 6px; background: var(--panel2);
  border: 1px solid var(--border2); border-radius: 6px; box-shadow: 0 8px 24px rgba(0,0,0,.5); }
.t { font-size: 12px; font-weight: 600; padding: 2px 6px 4px; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
.item { display: flex; align-items: center; gap: 8px; height: 32px; padding: 0 8px; border: 1px solid transparent; border-radius: 4px; background: none; text-align: left; font-size: 13px; }
.item:hover { background: var(--chip); border-color: var(--border); }
.danger { color: var(--warn); }
.danger.sure { background: var(--warn); color: #1d1300; font-weight: 700; }
.warn { font-size: 11px; color: var(--muted); padding: 2px 6px 2px; line-height: 1.4; }
</style>
