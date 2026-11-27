<script setup lang="ts">
// The LSM operator screen (/operator?operator=N): no navigation, a fluid layout from a phone to a console screen.
// The operator's channels on top, every camera below them synced to the channel under control, the clip bin at the
// bottom and the playlist in the sidebar. Every key acts on the channel under control.
import { onMounted, onUnmounted, ref } from "vue";
import ChannelPanel from "./components/ChannelPanel.vue";
import CameraRow from "./components/CameraRow.vue";
import ClipBin from "./components/ClipBin.vue";
import Icons from "./components/Icons.vue";
import KeysOverlay from "./components/KeysOverlay.vue";
import PlaylistSidebar from "./components/PlaylistSidebar.vue";
import SpeedMenu from "./components/SpeedMenu.vue";
import ClipMenu from "./components/ClipMenu.vue";
import { restoreXTouch } from "./lib/xtouch";
import {
  addToPlaylist,
  cameras,
  control,
  cueClip,
  mark,
  myChannels,
  nextControl,
  operator,
  playPause,
  scrubFrames,
  scrubSeconds,
  setAngle,
  start,
  state,
  takeControl,
  transport,
} from "./lib/store";

const showKeys = ref(false);
const listing = ref(false);

// ------------------------------------------------------------ full screen
function toggleFullscreen() {
  if (document.fullscreenElement) document.exitFullscreen();
  else document.documentElement.requestFullscreen?.();
}

// ------------------------------------------------------------ keys
function onKey(e: KeyboardEvent) {
  const target = e.target as HTMLElement;
  if (target.tagName === "INPUT" || target.tagName === "TEXTAREA") return;
  if (e.ctrlKey || e.metaKey || e.altKey) return;
  const k = e.key.toLowerCase();
  if (k === "?") return void (showKeys.value = !showKeys.value);
  if (k === "escape") {
    showKeys.value = false;
    listing.value = false;
    state.speedMenu = null;
    state.clipMenu = null;
    return;
  }
  if (k === "f") return toggleFullscreen();
  if (k === "tab") {
    e.preventDefault();
    return nextControl();
  }
  if (k === "a" || k === "b") {
    const ch = myChannels.value[k === "a" ? 0 : 1];
    return ch && takeControl(ch.index);
  }
  if (k === "p") return state.selectedClip && addToPlaylist(state.selectedClip);
  const c = control.value;
  if (!c) return;
  if (/^[1-9]$/.test(k)) {
    if (cameras.value.some((cam) => cam.index === Number(k))) setAngle(c.index, Number(k));
  } else if (k === " ") {
    e.preventDefault();
    playPause(c.index);
  } else if (k === "enter") {
    e.preventDefault();
    if (state.selectedClip) cueClip(c.index, state.selectedClip);
  } else if (k === "i") mark(c.index, "in");
  else if (k === "o") mark(c.index, "out");
  else if (k === "[") mark(c.index, "goto-in");
  else if (k === "]") mark(c.index, "goto-out");
  else if (k === "l") transport(c.index, "live");
  else if (k === "arrowleft" || k === "arrowright") {
    e.preventDefault();
    const dir = k === "arrowleft" ? -1 : 1;
    if (e.shiftKey) scrubSeconds(c.index, dir);
    else scrubFrames(c.index, dir);
  }
}

onMounted(() => {
  start();
  window.addEventListener("keydown", onKey);
  restoreXTouch();
});
onUnmounted(() => {
  window.removeEventListener("keydown", onKey);
});
</script>

<template>
  <Icons />
  <div v-if="state.status && myChannels.length" id="stage">
      <section id="channels" :class="`n${Math.min(myChannels.length, 4)}`">
        <ChannelPanel v-for="ch in myChannels" :key="ch.index" :ch="ch" />
      </section>
      <CameraRow @keys="showKeys = !showKeys" @fullscreen="toggleFullscreen" />
      <ClipBin />
      <PlaylistSidebar v-model:listing="listing" />
      <KeysOverlay v-if="showKeys" @close="showKeys = false" />
      <SpeedMenu />
      <ClipMenu />
  </div>
  <div v-else class="notice">
      <template v-if="state.indexing">The replay is indexing its buffer…</template>
      <template v-else-if="state.status && !myChannels.length">{{ operator?.label || "This operator" }} has no channels.</template>
      <template v-else-if="state.statusError">Replay unavailable: {{ state.statusError }}</template>
      <template v-else>Connecting to the replay…</template>
  </div>
</template>

<style scoped>
/* Channels side by side while each gets at least 420px, else stacked. */
#channels { display: grid; gap: 10px; min-height: 0; grid-template-columns: repeat(auto-fit, minmax(min(100%, 420px), 1fr)); }
@media (max-width: 759px) { #channels { gap: 6px; } }
</style>
