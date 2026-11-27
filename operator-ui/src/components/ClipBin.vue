<script setup lang="ts">
// The clips, newest first. One click selects; Enter or a double-click cues it on the channel under control
// (Space then plays). Drag a clip onto the playlist to add it there.
import { computed, nextTick, onMounted, onUnmounted, ref, watch, watchEffect } from "vue";
import { fmtDur, fmtSpeed, noThumb, playNs, tagsOf, thumbUrl, type Clip } from "../lib/api";
import Icon from "./Icon.vue";
import SelectedClip from "./SelectedClip.vue";
import { smoothPos } from "../lib/smooth";
import { cameraLabel, control, cueClip, letter, myChannels, state } from "../lib/store";
import { bin } from "../lib/xtouch";

// Tag filters: the most used tags; several chips filter with AND. Other tags: the search.
const filters = ref<string[]>([]);
const query = ref("");
const tags = computed(() => {
  const top = state.tags.filter((t) => t.clips > 0).slice(0, 8).map((t) => t.name);
  return [...top, ...filters.value.filter((f) => !top.includes(f))];
});
const has = (clip: Clip, tag: string) => tagsOf(clip).some((t) => t.toLowerCase() === tag.toLowerCase());
function toggleFilter(tag: string) {
  filters.value = filters.value.includes(tag) ? filters.value.filter((t) => t !== tag) : [...filters.value, tag];
}

const shown = computed(() => {
  const q = query.value.trim().toLowerCase();
  return state.clips.filter((c) => {
    if (!filters.value.every((t) => has(c, t))) return false;
    return !q || `${c.id} ${c.name} ${c.tags} ${cameraLabel(c.camera)}`.toLowerCase().includes(q);
  });
});

// The X-Touch's arrows walk the clips as shown: their order, and the columns of the grid for a row up or down.
watchEffect(() => (bin.order = shown.value.map((c) => c.id)));
const binEl = ref<HTMLElement | null>(null);
let resize: ResizeObserver | null = null;
onMounted(() => {
  resize = new ResizeObserver(() => {
    if (binEl.value) bin.columns = Math.max(1, getComputedStyle(binEl.value).gridTemplateColumns.split(" ").filter(Boolean).length);
  });
  if (binEl.value) resize.observe(binEl.value);
});
onUnmounted(() => resize?.disconnect());
// A clip selected from elsewhere (the X-Touch) is scrolled into view.
watch(
  () => state.selectedClip,
  (id) => nextTick(() => binEl.value?.querySelector(`[data-clip="${CSS.escape(id)}"]`)?.scrollIntoView({ block: "nearest" })),
);

/** The operator's channel showing a clip on its own (not in a playlist). */
function onChannel(clip: Clip) {
  return myChannels.value.find((ch) => ch.clip === clip.id && !ch.playlist && !ch.live) || null;
}
function progress(clip: Clip) {
  const ch = onChannel(clip);
  if (!ch) return 0;
  return Math.max(0, Math.min(100, (100 * (smoothPos(ch) - clip.in_ns)) / (clip.out_ns - clip.in_ns || 1)));
}

const select = (clip: Clip) => (state.selectedClip = clip.id);
function cue(clip: Clip) {
  select(clip);
  if (control.value) cueClip(control.value.index, clip.id);
}
function openSpeed(e: MouseEvent, clip: Clip) {
  const r = (e.currentTarget as HTMLElement).getBoundingClientRect();
  const m = state.speedMenu;
  state.speedMenu = m?.kind === "clip" && m.id === clip.id ? null : { kind: "clip", id: clip.id, index: 0, x: r.right, y: r.top };
}
function openMenu(e: MouseEvent, clip: Clip) {
  const r = (e.currentTarget as HTMLElement).getBoundingClientRect();
  state.speedMenu = null;
  state.clipMenu = state.clipMenu?.id === clip.id ? null : { id: clip.id, x: r.right, y: r.top };
}
function drag(e: DragEvent, clip: Clip) {
  e.dataTransfer?.setData("text/x-clip", clip.id);
  if (e.dataTransfer) e.dataTransfer.effectAllowed = "copy";
}
</script>

<template>
  <section id="bottom" class="pane">
    <div class="pane-head">
      <h2>Clips</h2>
      <button class="chip" :class="{ on: !filters.length }" @click="filters = []">All</button>
      <button v-for="t in tags" :key="t" class="chip" :class="{ on: filters.includes(t) }" @click="toggleFilter(t)">{{ t }}</button>
      <span class="spacer"></span>
      <span v-if="control" class="hint">Double-click or Enter cues on <b>{{ control.label }}</b></span>
      <input v-model="query" class="search" placeholder="Search name or tag" @keydown.esc="($event.target as HTMLInputElement).blur()" />
    </div>
    <div ref="binEl" class="bin">
      <div
        v-for="clip in shown"
        :key="clip.id"
        :data-clip="clip.id"
        class="clip"
        :class="{ sel: state.selectedClip === clip.id, cued: onChannel(clip) && !onChannel(clip)!.playing, playing: onChannel(clip)?.playing }"
        draggable="true"
        @click="select(clip)"
        @dblclick="cue(clip)"
        @dragstart="drag($event, clip)"
      >
        <img class="thumb" :src="thumbUrl(clip)" alt="" loading="lazy" width="64" height="36" @error="noThumb" />
        <span class="prog" :style="{ width: `${progress(clip)}%` }"></span>
        <span v-if="onChannel(clip)" class="chtag">{{ letter(onChannel(clip)!) }} {{ onChannel(clip)!.playing ? "PLAY" : "CUED" }}</span>
        <div class="meta">
          <div class="n"><span class="sw" :style="{ background: clip.colour }"></span>{{ clip.name }}</div>
          <div class="s"><span class="tc when" :title="`IN ${clip.in_tc} · OUT ${clip.out_tc}`">{{ clip.in_tc.slice(0, 8) }}</span> · {{ clip.camera }} {{ cameraLabel(clip.camera) }}<template v-if="clip.tags"> · {{ tagsOf(clip).join(" · ") }}</template></div>
        </div>
        <div class="r">
          <span class="tc">{{ fmtDur(playNs(clip)) }}</span>
          <span class="r2">
            <button class="spd" data-speed-badge title="Clip speed: click for presets" @click.stop="openSpeed($event, clip)" @dblclick.stop>{{ fmtSpeed(clip.speed) }}</button>
            <button class="more" data-clip-menu title="More: duplicate, delete" :aria-label="`More for ${clip.name}`" @click.stop="openMenu($event, clip)" @dblclick.stop><Icon name="more" /></button>
          </span>
        </div>
      </div>
      <div v-if="!shown.length" class="empty" style="grid-column: 1 / -1">
        {{ state.clips.length ? "No clip matches. Clear the search or pick All." : "No clips yet. Mark IN, then OUT on a channel." }}
      </div>
    </div>
    <SelectedClip />
  </section>
</template>

<style scoped>
#bottom { display: grid; grid-template-rows: auto minmax(0, 1fr) auto; min-height: 0; }
.search { background: var(--sunk); border: 1px solid var(--border); border-radius: 5px; color: var(--text); padding: 5px 8px; font-size: 12px; width: 170px; min-width: 0; flex: 0 1 170px; }
.pane-head { flex-wrap: wrap; }
@media (max-width: 1100px) { .hint { display: none; } }
@media (pointer: coarse) { .search { font-size: 16px; } }
.search::placeholder { color: var(--muted); }
.bin { display: grid; grid-template-columns: repeat(auto-fill, minmax(min(100%, 260px), 1fr)); gap: 4px 6px; padding: 8px; overflow-y: auto; align-content: start; }
.clip { position: relative; display: grid; grid-template-columns: 64px 1fr auto; gap: 8px; align-items: center; height: 44px; padding: 3px 7px 3px 4px;
  background: var(--panel2); border: 1px solid var(--border); border-radius: 4px; overflow: hidden; cursor: pointer; user-select: none; transition: border-color .12s, background-color .12s; }
.clip:hover { border-color: var(--muted); }
.clip.sel { border-color: var(--accent); background: var(--accent-tint); }
.clip.sel .s { color: var(--muted-on-tint); }
.clip.cued { border-color: var(--cue); }
.clip.playing { border-color: var(--ok); }
.thumb { width: 64px; height: 36px; border-radius: 2px; }
.meta { min-width: 0; font-size: 12px; line-height: 1.3; }
.n { font-weight: 600; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
.s { color: var(--muted); font-size: 11px; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
.r { display: flex; flex-direction: column; align-items: flex-end; gap: 2px; font-size: 11px; }
.when { color: var(--text); opacity: .8; }
.r2 { display: flex; gap: 4px; align-items: center; }
.spd { font: 700 11px var(--mono); padding: 0 5px; line-height: 16px; border-radius: 3px; border: 1px solid var(--border2); background: var(--sunk); color: var(--text); }
.spd:hover { border-color: var(--accent); }
.more { display: grid; place-items: center; width: 22px; height: 18px; padding: 0; border-radius: 3px; border: 1px solid transparent; background: none; color: var(--muted); }
.more:hover { color: var(--text); border-color: var(--border2); }
/* Over the thumbnail, so a cue does not move the card's text. */
.chtag { position: absolute; left: 7px; bottom: 6px; font-size: 10px; font-weight: 800; padding: 0 4px; border-radius: 3px; line-height: 14px; }
.clip.cued .chtag { background: var(--cue); color: #1d1600; }
.clip.playing .chtag { background: var(--ok); color: #06140b; }
.prog { position: absolute; left: 0; bottom: 0; height: 2px; background: var(--ok); }
</style>
