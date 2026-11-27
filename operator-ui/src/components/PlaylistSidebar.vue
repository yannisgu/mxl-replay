<script setup lang="ts">
// The sidebar: the open playlist's entries, or (the picker) the list of every playlist in its place.
// Entries: drag clips in from the bin, drag entries to reorder, × removes. Cue on A/B cues the playlist's start.
// A click opens an entry: its clip's IN/OUT (set on a channel: cue it there, mark IN and OUT, Update clip), end,
// auto-advance, cue from it, a copy of its clip for it. The speed badge opens presets; the pill between two entries
// sets the transition into the second (cut, or a mix in frames).
import { computed, onMounted, onUnmounted, ref } from "vue";
import { fmtDur, fmtSpeed, noThumb, playNs, thumbUrl, type PlaylistEntry, type PlaylistSummary } from "../lib/api";
import {
  addToPlaylist,
  cameraLabel,
  clipsById,
  copyForEntry,
  cuePlaylist,
  letter,
  myChannels,
  newPlaylist,
  openPlaylist,
  setEntries,
  setEntry,
  state,
  toggleLoop,
} from "../lib/store";
import Icon from "./Icon.vue";

const listing = defineModel<boolean>("listing", { default: false });
const END = ["freeze", "black", "loop", "next", "return-to-live"];
const open = ref(-1);
const joinOpen = ref(-1);

const pl = computed(() => state.playlist);
const summary = computed(() => state.playlists.find((p) => p.id === state.playlistId) || null);

const rows = computed(() =>
  (pl.value?.entries || []).map((e, i) => {
    const clip = clipsById.value[e.clip_id] || null;
    const on = myChannels.value.find((c) => c.playlist === pl.value?.id && c.playlist_index === i && !c.live) || null;
    const next = myChannels.value.find((c) => c.playlist === pl.value?.id && c.playing && c.playlist_index + 1 === i) || null;
    return { e, i, clip, on, next, dur: clip ? playNs(clip, e.speed) : 0 };
  }),
);

/** A channel of this operator on a playlist. */
const onChannel = (p: PlaylistSummary) => myChannels.value.find((c) => c.playlist === p.id && !c.live) || null;

function pick(p: PlaylistSummary) {
  openPlaylist(p.id);
  listing.value = false;
}
async function create() {
  await newPlaylist();
  listing.value = false;
}

// Drag and drop: clips from the bin, entries within the list. While dragging, a line shows where it lands (none where a
// moved entry would stay where it is); near the top or bottom of the list it scrolls.
const box = ref<HTMLElement | null>(null);
const dragFrom = ref(-1);
const dropAt = ref(-1);
const dragKind = ref<"clip" | "entry" | "">("");
function dropIndex(e: DragEvent): number {
  const items = [...(box.value?.querySelectorAll<HTMLElement>(".entry") || [])];
  const at = items.findIndex((r) => {
    const b = r.getBoundingClientRect();
    return e.clientY < b.top + b.height / 2;
  });
  return at < 0 ? items.length : at;
}
const stays = (at: number) => dragFrom.value >= 0 && (at === dragFrom.value || at === dragFrom.value + 1);
function startEntry(e: DragEvent, i: number) {
  dragFrom.value = i;
  e.dataTransfer?.setData("text/x-entry", String(i));
  if (e.dataTransfer) e.dataTransfer.effectAllowed = "move";
}
function endDrag() {
  dragFrom.value = -1;
  dropAt.value = -1;
  dragKind.value = "";
}
function over(e: DragEvent) {
  const types = e.dataTransfer?.types || [];
  const kind = types.includes("text/x-entry") ? "entry" : types.includes("text/x-clip") ? "clip" : "";
  if (!kind || !pl.value) return;
  e.preventDefault();
  dragKind.value = kind;
  const at = dropIndex(e);
  dropAt.value = stays(at) ? -1 : at;
  if (e.dataTransfer) e.dataTransfer.dropEffect = dropAt.value < 0 ? "none" : kind === "entry" ? "move" : "copy";
  const b = box.value!.getBoundingClientRect();
  if (e.clientY < b.top + 40) box.value!.scrollTop -= 14;
  else if (e.clientY > b.bottom - 40) box.value!.scrollTop += 14;
}
// A drag that ends anywhere (dropped elsewhere, or Esc) takes the line away.
onMounted(() => window.addEventListener("dragend", endDrag));
onUnmounted(() => window.removeEventListener("dragend", endDrag));
function leave(e: DragEvent) {
  if (!box.value?.contains(e.relatedTarget as Node | null)) dropAt.value = -1;
}
function drop(e: DragEvent) {
  const at = dropAt.value;
  const clipId = e.dataTransfer?.getData("text/x-clip");
  const from = e.dataTransfer?.getData("text/x-entry");
  endDrag();
  if (!pl.value || at < 0) return;
  if (clipId) addToPlaylist(clipId, at);
  else if (from) {
    const entries = [...pl.value.entries];
    const [moved] = entries.splice(Number(from), 1);
    entries.splice(at > Number(from) ? at - 1 : at, 0, moved);
    if (open.value === Number(from)) open.value = at > Number(from) ? at - 1 : at;
    setEntries(entries);
  }
}
function remove(i: number) {
  if (!pl.value) return;
  const entries = [...pl.value.entries];
  entries.splice(i, 1);
  open.value = -1;
  setEntries(entries);
}

function openSpeed(e: MouseEvent, kind: "entry" | "all", index = 0) {
  if (!pl.value) return;
  const r = (e.currentTarget as HTMLElement).getBoundingClientRect();
  const m = state.speedMenu;
  state.speedMenu = m && m.kind === kind && m.index === index ? null : { kind, id: pl.value.id, index, x: r.right, y: r.top };
}
const followsClip = (e: PlaylistEntry) => {
  const c = clipsById.value[e.clip_id];
  return !!c && Math.abs(c.speed - e.speed) < 0.005;
};
const edit = (i: number, fields: Partial<PlaylistEntry>) => pl.value && setEntry(pl.value.id, i, fields);
function setMix(i: number, frames: number) {
  edit(i, { transition: "mix", transition_frames: Math.max(1, Math.min(250, Math.round(frames) || 25)) });
}
const joinText = (e: PlaylistEntry) => (e.transition === "mix" ? `Mix ${e.transition_frames}` : "Cut");
</script>

<template>
  <aside id="side" class="pane">
    <div v-if="listing" class="view">
      <div class="plbar">
        <button class="b" title="Back (Esc)" aria-label="Back" @click="listing = false"><Icon name="left" /></button>
        <span class="title">Playlists</span>
      </div>
      <div class="plist">
        <button v-for="p in state.playlists" :key="p.id" class="plrow" :class="{ cur: p.id === state.playlistId }" @click="pick(p)">
          <span class="stack">
            <template v-for="id in p.clip_ids.slice(0, 3)" :key="id">
              <img v-if="clipsById[id]" class="thumb" :src="thumbUrl(clipsById[id])" alt="" loading="lazy" @error="noThumb" />
            </template>
          </span>
          <span class="txt">
            <span class="nm">{{ p.name }}</span>
            <span class="sub">
              <template v-if="p.entries">{{ p.entries }} clips · <span class="tc">{{ fmtDur(p.duration_ns) }}</span></template>
              <template v-else>Empty</template>
              <template v-if="p.loop"> · loops</template>
            </span>
          </span>
          <span>
            <span v-if="onChannel(p)" class="st" :class="onChannel(p)!.playing ? 'play' : 'cue'">{{ letter(onChannel(p)!) }} {{ onChannel(p)!.playing ? "PLAY" : "CUED" }}</span>
          </span>
        </button>
        <div v-if="!state.playlists.length" class="empty">No playlists yet.</div>
      </div>
      <div class="foot"><button class="b wide" @click="create"><Icon name="plus" />New playlist</button></div>
    </div>

    <div v-else class="view">
      <div class="plbar">
        <button class="plpick" title="All playlists" @click="listing = true">
          <Icon name="list" /><span>{{ summary?.name || "No playlist" }}</span><Icon name="right" />
        </button>
        <button class="b" data-speed-badge :disabled="!pl?.entries.length" title="Set every entry's speed" @click="openSpeed($event, 'all')">All speeds</button>
        <button class="b" :class="{ on: pl?.loop }" :disabled="!pl" title="Loop the playlist" @click="toggleLoop"><Icon name="loop" />Loop</button>
      </div>
      <div ref="box" class="entries" :class="{ dragging: dragKind }" @dragover="over" @dragleave="leave" @drop.prevent="drop">
        <template v-for="r in rows" :key="`${r.i}-${r.e.clip_id}`">
          <div v-if="dropAt === r.i" class="dropline"><span>{{ dragKind === "entry" ? "Move" : "Add" }} here · {{ r.i + 1 }}</span></div>
          <div v-if="r.i > 0 || pl?.loop" class="join">
            <button class="pill" :class="{ mix: r.e.transition === 'mix', open: joinOpen === r.i, wrap: r.i === 0 }" :title="r.i === 0 ? 'Into the first entry when the playlist loops' : `Into entry ${r.i + 1}`" @click="joinOpen = joinOpen === r.i ? -1 : r.i">
              {{ joinText(r.e) }}
            </button>
            <div v-if="joinOpen === r.i" class="joined">
              <span class="seg">
                <button :class="{ on: r.e.transition !== 'mix' }" @click="edit(r.i, { transition: 'cut', transition_frames: 0 })">Cut</button>
                <button :class="{ on: r.e.transition === 'mix' }" @click="setMix(r.i, r.e.transition_frames || 25)">Mix</button>
              </span>
              <template v-if="r.e.transition === 'mix'">
                <button v-for="n in [12, 25, 50]" :key="n" class="b sm" :class="{ on: r.e.transition_frames === n }" @click="setMix(r.i, n)">{{ n }}</button>
                <input type="number" min="1" max="250" :value="r.e.transition_frames" aria-label="Mix length in frames" @change="setMix(r.i, Number(($event.target as HTMLInputElement).value))" />
                <span class="hint">frames</span>
              </template>
            </div>
          </div>
          <div
            class="entry"
            :class="{ playing: r.on?.playing, cued: r.on && !r.on.playing, gone: !r.clip, opened: open === r.i, moving: dragFrom === r.i }"
            draggable="true"
            @dragstart="startEntry($event, r.i)"
            @dragend="endDrag"
          >
            <div class="erow" @click="open = open === r.i ? -1 : r.i">
              <span class="idx">{{ r.i + 1 }}</span>
              <img v-if="r.clip" class="thumb" :src="thumbUrl(r.clip)" alt="" loading="lazy" @error="noThumb" />
              <span v-else class="thumb"></span>
              <span v-if="r.on" class="st over" :class="r.on.playing ? 'air' : 'cue'">{{ letter(r.on) }} {{ r.on.playing ? "PLAY" : "CUED" }}</span>
              <span v-else-if="r.next" class="st over next">NEXT</span>
              <span class="n">
                {{ r.clip?.name || `${r.e.clip_id} (deleted, skipped)` }}
                <small v-if="r.clip">{{ r.clip.camera }} {{ cameraLabel(r.clip.camera) }} · {{ r.e.end }}{{ r.e.auto_advance ? "" : " · waits" }}</small>
              </span>
              <span class="rt">
                <span class="tc">{{ fmtDur(r.dur) }}</span>
                <span class="r2">
                  <button class="spd" :class="{ follow: followsClip(r.e) }" data-speed-badge :title="followsClip(r.e) ? 'The clip\'s speed: click for presets' : 'The entry\'s own speed: click for presets'" @click.stop="openSpeed($event, 'entry', r.i)">
                    <Icon v-if="followsClip(r.e)" name="link" />{{ fmtSpeed(r.e.speed) }}
                  </button>
                </span>
              </span>
              <button class="x" title="Remove from playlist" aria-label="Remove from playlist" @click.stop="remove(r.i)"><Icon name="x" /></button>
            </div>
            <div v-if="open === r.i && r.clip" class="edit">
              <div class="io">
                <span class="tc">IN {{ r.clip.in_tc }}</span><span class="tc">OUT {{ r.clip.out_tc }}</span><span class="tc muted">{{ fmtDur(r.clip.out_ns - r.clip.in_ns) }}</span>
              </div>
              <span class="hint">{{ r.on ? `On ${letter(r.on)}: IN and OUT there change the clip at once.` : "To change IN/OUT, cue it and mark IN and OUT: they change the clip at once." }}{{ r.clip && state.playlists.filter((p) => p.clip_ids.includes(r.clip!.id)).length + (pl?.entries.filter((x) => x.clip_id === r.e.clip_id).length || 0) > 2 ? " The clip is used elsewhere too: Make a copy first to change only this entry." : "" }}</span>
              <div class="frow">
                <label>End <select :value="r.e.end" @change="edit(r.i, { end: ($event.target as HTMLSelectElement).value })"><option v-for="v in END" :key="v">{{ v }}</option></select></label>
                <label class="check"><input type="checkbox" :checked="r.e.auto_advance" @change="edit(r.i, { auto_advance: ($event.target as HTMLInputElement).checked })" /> Auto-advance</label>
              </div>
              <div class="frow">
                <button v-for="c in myChannels" :key="c.index" class="b sm" @click="cuePlaylist(c.index, pl!.id, r.i)">Cue on {{ letter(c) }}</button>
                <button class="b sm" :disabled="!r.on" :title="r.on ? 'A copy of the clip for this entry only: its IN/OUT then change only the copy' : 'Cue the entry on a channel first'" @click="r.on && copyForEntry(r.on.index)">Make a copy</button>
              </div>
            </div>
          </div>
        </template>
        <div v-if="dropAt >= 0 && dropAt === rows.length" class="dropline end"><span>{{ dragKind === "entry" ? "Move" : "Add" }} here · {{ rows.length + 1 }}</span></div>
        <div v-if="pl && !rows.length" class="empty">Drag clips here, or select a clip and press P.</div>
        <div v-if="!summary" class="empty">No playlist. Open the list to make one.</div>
      </div>
      <div class="foot">
        <span class="total">
          <template v-if="pl"><span class="tc">{{ fmtDur(pl.duration_ns) }}</span><template v-if="pl.loop"> · loops</template></template>
        </span>
        <button class="b wide" :disabled="!pl || !state.selectedClip" @click="addToPlaylist(state.selectedClip)"><Icon name="plus" />Add selected clip <kbd>P</kbd></button>
        <button v-for="c in myChannels" :key="c.index" class="b" :class="{ wide: myChannels.length === 1 }" :disabled="!pl?.entries.length" @click="pl && cuePlaylist(c.index, pl.id)">
          Cue on {{ letter(c) }}
        </button>
      </div>
    </div>
  </aside>
</template>

<style scoped>
#side { display: grid; grid-template-rows: 1fr; container-type: inline-size; }
/* A narrow sidebar: the playlist's name on its own row, its two buttons sharing the next. */
@container (max-width: 360px) { .plbar .plpick { flex-basis: 100%; } .plbar:has(.plpick) .b { flex: 1; } }
.view { display: grid; grid-template-rows: auto 1fr auto; min-height: 0; }
.plbar { display: flex; flex-wrap: wrap; gap: 6px; padding: 10px 10px 4px; }
.plbar .b { height: 36px; }
.title { align-self: center; font-size: 15px; font-weight: 700; }
.plpick { flex: 1 1 150px; display: flex; align-items: center; gap: 8px; height: 36px; padding: 0 10px 0 12px; font: 700 15px system-ui, sans-serif;
  background: var(--chip); border: 1px solid var(--border2); border-radius: 5px; text-align: left; min-width: 0; }
.plpick:hover { border-color: var(--muted); }
.plpick span { flex: 1; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
.plpick :deep(svg) { color: var(--muted); }
.plist { overflow-y: auto; padding: 6px 8px; display: flex; flex-direction: column; gap: 4px; }
.plrow { display: grid; grid-template-columns: 112px 1fr 64px; gap: 10px; align-items: center; padding: 6px; border: 1px solid transparent; border-radius: 6px; background: none; text-align: left; }
.plrow:hover { background: var(--panel2); border-color: var(--border2); }
.plrow.cur { border-color: var(--accent); background: var(--accent-tint); }
.stack { position: relative; width: 112px; height: 54px; }
.stack .thumb { position: absolute; width: 80px; height: 45px; border-radius: 3px; border: 1px solid var(--bg); }
.stack .thumb:nth-child(1) { left: 0; top: 0; z-index: 3; }
.stack .thumb:nth-child(2) { left: 16px; top: 4px; z-index: 2; opacity: .85; }
.stack .thumb:nth-child(3) { left: 32px; top: 8px; z-index: 1; opacity: .7; }
.txt { min-width: 0; display: flex; flex-direction: column; }
.nm { font-size: 14px; font-weight: 700; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
.sub { font-size: 12px; color: var(--muted); }
.plrow.cur .sub { color: var(--muted-on-tint); }
.st { font-size: 11px; font-weight: 700; }
.plrow .st { padding: 1px 6px; border-radius: 3px; }
.plrow .st.play { background: var(--ok); color: #06140b; }
.plrow .st.cue { background: var(--cue); color: #1d1600; }
.entries { overflow-y: auto; padding: 12px 8px 6px; }
.entries.dragging { background: rgba(77,163,255,.05); }
/* Where a dragged clip or entry lands: a line between two entries that takes no room, so nothing jumps. */
.dropline { position: relative; height: 0; z-index: 2; pointer-events: none; }
.dropline::before { content: ""; position: absolute; left: 0; right: 0; top: -2px; height: 4px; border-radius: 2px; background: var(--accent); box-shadow: 0 0 0 2px rgba(77,163,255,.25); }
.dropline span { position: absolute; right: 8px; top: -10px; line-height: 16px; font-size: 11px; font-weight: 700; color: #031426; background: var(--accent); border-radius: 99px; padding: 2px 8px; }
.dropline.end { height: 24px; }
.dropline.end::before { top: 4px; }
.dropline.end span { top: -5px; }
.entry.moving { opacity: .35; }
.entry { border-radius: 5px; font-size: 12px; border: 1px solid transparent; cursor: grab; }
.erow { position: relative; display: grid; grid-template-columns: 20px 80px 1fr auto 28px; gap: 8px; align-items: center; padding: 5px 6px; cursor: pointer; }
/* The entry's state over its thumbnail, so a cue does not move the row's text. */
.st.over { position: absolute; left: 37px; bottom: 8px; font-size: 10px; font-weight: 800; line-height: 14px; padding: 0 4px; border-radius: 3px; background: rgba(11,15,20,.85); }
.entry:hover { background: var(--panel2); }
.entry.opened { border-color: var(--accent); background: var(--panel2); }
.edit { display: flex; flex-direction: column; gap: 8px; padding: 4px 10px 10px 34px; cursor: default; }
.io { display: flex; gap: 12px; flex-wrap: wrap; font-size: 12px; }
.muted { color: var(--muted); }
.frow { display: flex; gap: 8px 12px; flex-wrap: wrap; align-items: center; }
.frow label { display: inline-flex; gap: 6px; align-items: center; color: var(--muted); }
select, input[type="number"] { background: var(--sunk); border: 1px solid var(--border); border-radius: 5px; color: var(--text); padding: 3px 6px; font: inherit; font-size: 12px; }
input[type="number"] { width: 60px; font-family: var(--mono); }
.check { cursor: pointer; }
.b.sm { height: 26px; font-size: 12px; padding: 0 8px; min-width: 0; }
.r2 { display: flex; gap: 4px; align-items: center; }
.spd { display: inline-flex; align-items: center; gap: 3px; font: 700 11px var(--mono); padding: 0 5px; line-height: 16px; border-radius: 3px; border: 1px solid var(--border2); background: var(--sunk); color: var(--text); }
.spd:hover { border-color: var(--accent); }
.spd.follow { border-style: dashed; color: var(--muted); }
.spd :deep(svg) { width: 11px; height: 11px; }
.join { display: flex; flex-direction: column; align-items: center; padding: 2px 0; }
.pill { border: 1px solid var(--border2); background: var(--sunk); border-radius: 99px; padding: 0 10px; font: 700 11px var(--mono); color: var(--muted); line-height: 18px; }
.pill:hover { border-color: var(--muted); }
.pill.mix { color: var(--cue); border-color: rgba(232,195,58,.5); }
.pill.open { border-color: var(--accent); color: var(--accent); }
.pill.wrap { opacity: .6; }
.joined { display: flex; flex-wrap: wrap; gap: 6px; align-items: center; justify-content: center; margin: 4px 0; padding: 6px 8px; border: 1px solid var(--accent); border-radius: 6px; background: var(--panel); }
.seg { display: inline-flex; border: 1px solid var(--border); border-radius: 5px; overflow: hidden; }
.seg button { background: var(--chip); border: 0; padding: 3px 10px; font-size: 12px; font-weight: 600; color: var(--muted); }
.seg button + button { border-left: 1px solid var(--border); }
.seg button.on { background: var(--accent); color: #031426; }
.entry.playing { border-color: var(--ok); background: rgba(63,185,104,.08); }
.entry.cued { border-color: var(--cue); background: rgba(232,195,58,.06); }
.entry.gone { opacity: .5; }
.idx { color: var(--muted); font-family: var(--mono); text-align: right; }
.entry .thumb { width: 80px; height: 45px; border-radius: 3px; }
.n { font-size: 13px; font-weight: 600; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; min-width: 0; }
.n small { display: block; color: var(--muted); font-weight: 400; font-size: 12px; overflow: hidden; text-overflow: ellipsis; }
.rt { display: flex; flex-direction: column; align-items: flex-end; gap: 1px; font-size: 12px; }
.rt .sub { font-size: 11px; }
.st.air { color: var(--ok); }
.st.cue { color: var(--cue); }
.st.next { color: var(--muted); }
.x { display: grid; place-items: center; width: 26px; height: 26px; background: none; border: 1px solid transparent; border-radius: 4px; color: var(--muted); }
.x:hover { color: var(--text); border-color: var(--border2); }
.foot { display: grid; grid-template-columns: 1fr 1fr; gap: 6px; align-items: center; padding: 10px; border-top: 1px solid var(--border); font-size: 12px; }
.foot .b { height: 36px; }
.foot .wide, .total { grid-column: 1 / -1; }
.total { color: var(--muted); }
</style>
