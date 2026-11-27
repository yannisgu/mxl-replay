<script setup lang="ts">
// The selected clip (a click in the bin; a clip OUT made is selected): its name (Enter renames), its tags (× removes),
// the most used tags to add with a click, and a box that finds any tag as it is typed or makes a new one (Enter).
import { computed, ref, watch } from "vue";
import { fmtDur, fmtSpeed, noThumb, playNs, tagsOf, thumbUrl } from "../lib/api";
import { addClipTag, cameraLabel, clipsById, letter, loadedClip, myChannels, removeClipTag, renameClip, state } from "../lib/store";
import Icon from "./Icon.vue";

const MOST_USED = 8;

const clip = computed(() => (state.selectedClip ? clipsById.value[state.selectedClip] || null : null));
const has = computed(() => new Set((clip.value ? tagsOf(clip.value) : []).map((t) => t.toLowerCase())));
const onChannels = computed(() => (clip.value ? myChannels.value.filter((c) => !c.live && loadedClip(c)?.id === clip.value!.id) : []));

// name: edited in place, stored on Enter or when the field is left
const name = ref("");
watch(
  () => [clip.value?.id, clip.value?.name],
  () => (name.value = clip.value?.name || ""),
  { immediate: true },
);
function saveName() {
  if (clip.value) renameClip(clip.value.id, name.value);
}

// the most used tags the clip does not have
const suggestions = computed(() => state.tags.filter((t) => t.clips > 0 && !has.value.has(t.name.toLowerCase())).slice(0, MOST_USED));

// the box: tags containing the text (those starting with it first), then "Create …" when no tag is exactly it
const text = ref("");
const open = ref(false);
const active = ref(0);
const matches = computed(() => {
  const q = text.value.trim().toLowerCase();
  if (!q) return [];
  const found = state.tags.filter((t) => t.name.toLowerCase().includes(q) && !has.value.has(t.name.toLowerCase()));
  found.sort((a, b) => Number(b.name.toLowerCase().startsWith(q)) - Number(a.name.toLowerCase().startsWith(q)));
  return found.slice(0, 8);
});
const exact = computed(() => state.tags.some((t) => t.name.toLowerCase() === text.value.trim().toLowerCase()));
const options = computed(() => [...matches.value.map((t) => ({ name: t.name, clips: t.clips, create: false })), ...(text.value.trim() && !exact.value ? [{ name: text.value.trim(), clips: 0, create: true }] : [])]);
watch(text, () => {
  active.value = 0;
  open.value = !!text.value.trim();
});

function add(tag: string) {
  if (!clip.value) return;
  addClipTag(clip.value.id, tag);
  text.value = "";
  open.value = false;
}
function key(e: KeyboardEvent) {
  const n = options.value.length;
  if (e.key === "ArrowDown" && n) {
    e.preventDefault();
    active.value = (active.value + 1) % n;
  } else if (e.key === "ArrowUp" && n) {
    e.preventDefault();
    active.value = (active.value - 1 + n) % n;
  } else if (e.key === "Enter") {
    e.preventDefault();
    const pick = options.value[active.value];
    if (pick) add(pick.name);
  } else if (e.key === "Escape") {
    if (text.value) text.value = "";
    else (e.target as HTMLInputElement).blur();
  }
}
</script>

<template>
  <section class="selected" :class="{ none: !clip }">
    <template v-if="clip">
      <img class="thumb" :src="thumbUrl(clip)" alt="" @error="noThumb" />
      <div class="main">
        <div class="line">
          <input v-model="name" class="name" aria-label="Clip name" title="Rename: Enter" @keydown.enter="($event.target as HTMLInputElement).blur()" @keydown.esc="name = clip.name; ($event.target as HTMLInputElement).blur()" @blur="saveName" />
          <span class="meta">
            {{ clip.camera }} {{ cameraLabel(clip.camera) }} · <span class="tc">{{ clip.in_tc }}</span> · <span class="tc">{{ fmtDur(playNs(clip)) }}</span> at {{ fmtSpeed(clip.speed) }}
          </span>
          <span v-for="c in onChannels" :key="c.index" class="on">{{ letter(c) }} {{ c.playing ? "PLAY" : "CUED" }}</span>
        </div>
        <div class="tags">
          <span class="chips" @wheel.prevent="($event.currentTarget as HTMLElement).scrollLeft += $event.deltaY || $event.deltaX">
            <span v-for="t in tagsOf(clip)" :key="t" class="tag has">
              {{ t }}<button class="x" :aria-label="`Remove ${t}`" title="Remove" @click="removeClipTag(clip.id, t)"><Icon name="x" /></button>
            </span>
            <button v-for="t in suggestions" :key="t.name" class="tag add" :title="`Add ${t.name} (${t.clips} clips)`" @click="add(t.name)">+ {{ t.name }}</button>
          </span>
          <span class="box">
            <input
              v-model="text"
              class="find"
              placeholder="Tag…"
              aria-label="Find or create a tag"
              role="combobox"
              :aria-expanded="open && options.length > 0"
              @keydown="key"
              @focus="open = !!text.trim()"
              @blur="open = false"
            />
            <div v-if="open && options.length" class="list" role="listbox">
              <button
                v-for="(o, i) in options"
                :key="o.name + o.create"
                class="opt"
                :class="{ act: i === active }"
                role="option"
                :aria-selected="i === active"
                @mousedown.prevent="add(o.name)"
                @mousemove="active = i"
              >
                <template v-if="o.create">Create <b>{{ o.name }}</b></template>
                <template v-else>{{ o.name }}<small>{{ o.clips }}</small></template>
              </button>
            </div>
          </span>
        </div>
      </div>
    </template>
    <span v-else class="hint">Select a clip to rename and tag it.</span>
  </section>
</template>

<style scoped>
.selected { display: flex; gap: 10px; align-items: flex-start; min-height: 78px; padding: 8px 10px; border-top: 1px solid var(--border); background: var(--panel2); min-width: 0; }
.selected.none { align-items: center; }
.thumb { width: 96px; height: 54px; border-radius: 3px; flex: none; }
.main { flex: 1; min-width: 0; display: flex; flex-direction: column; gap: 6px; }
.line { display: flex; align-items: center; gap: 10px; min-width: 0; }
.line .name { flex: none; }
.line .on { flex: none; }
.name { background: var(--sunk); border: 1px solid var(--border); border-radius: 5px; color: var(--text); padding: 4px 8px; font: 700 14px system-ui, sans-serif; width: min(260px, 100%); }
.name:focus { border-color: var(--accent); }
.meta { font-size: 12px; color: var(--muted); white-space: nowrap; overflow: hidden; text-overflow: ellipsis; min-width: 0; }
.on { font-size: 11px; font-weight: 700; padding: 0 5px; border-radius: 3px; background: var(--cue); color: #1d1600; }
/* One line: the clip's tags and the most used scroll sideways when they do not fit, so the panel keeps its height. */
.tags { display: flex; gap: 5px; align-items: center; min-width: 0; }
.chips { display: flex; gap: 5px; align-items: center; min-width: 0; overflow-x: auto; scrollbar-width: none; }
.chips::-webkit-scrollbar { display: none; }
.chips > * { flex: none; }
.box { flex: none; }
.tag { display: inline-flex; align-items: center; height: 26px; border-radius: 99px; font-size: 12px; font-weight: 600; white-space: nowrap; }
.tag.has { background: var(--accent); color: #031426; padding-left: 10px; }
.tag.add { background: var(--chip); border: 1px dashed var(--border2); color: var(--muted); padding: 0 10px; }
.tag.add:hover { color: var(--text); border-color: var(--accent); }
.x { display: grid; place-items: center; width: 24px; height: 24px; background: none; border: 0; color: inherit; opacity: .7; }
.x:hover { opacity: 1; }
.x :deep(svg) { width: 11px; height: 11px; }
.box { position: relative; }
.find { width: 130px; height: 26px; background: var(--sunk); border: 1px solid var(--border2); border-radius: 99px; color: var(--text); padding: 0 10px; font-size: 12px; }
.find:focus { border-color: var(--accent); width: 170px; }
.list { position: absolute; left: 0; bottom: calc(100% + 4px); z-index: 20; min-width: 200px; max-height: 260px; overflow-y: auto; display: flex; flex-direction: column; padding: 4px; background: var(--panel2); border: 1px solid var(--border2); border-radius: 6px; box-shadow: 0 8px 24px rgba(0,0,0,.5); }
.opt { display: flex; justify-content: space-between; gap: 12px; text-align: left; padding: 6px 8px; border: 0; border-radius: 4px; background: none; font-size: 13px; }
.opt small { color: var(--muted); font-family: var(--mono); }
.opt.act { background: var(--accent-tint); }
@media (max-width: 759px) { .thumb { display: none; } }
@media (pointer: coarse) { .tag, .find { height: 34px; } }
</style>
