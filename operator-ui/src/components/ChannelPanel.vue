<script setup lang="ts">
// One LSM channel: header (letter, name, what it is on), the monitor and the playback strip under it.
// Only the header and the monitor take control; a button acts on its own channel without moving control.
import { computed, ref } from "vue";
import { fmtDur, fmtSpeed, type Channel } from "../lib/api";
import {
  cameraLabel,
  channelClip,
  channelEntry,
  editedClip,
  clipsById,
  controlIndex,
  faderSpeed,
  loadedClip,
  letter,
  mark,
  newestNs,
  onAir,
  playPause,
  saveSpeed,
  scrubFrames,
  scrubSeconds,
  setSpeed,
  state,
  takeControl,
  transport,
  uncue,
} from "../lib/store";
import BufferTimeline from "./BufferTimeline.vue";
import Icon from "./Icon.vue";
import Picture from "./Picture.vue";
import SpeedFader from "./SpeedFader.vue";
import { smoothPos, smoothTimecode } from "../lib/smooth";

const props = defineProps<{ ch: Channel }>();
const SPEEDS = [0.25, 0.33, 0.5, 0.75, 1];

const active = computed(() => controlIndex.value === props.ch.index);
const playing = computed(() => props.ch.playing && !props.ch.live);
const playlist = computed(() => (props.ch.playlist ? state.playlists.find((p) => p.id === props.ch.playlist) || null : null));
const clip = computed(() => (props.ch.clip ? clipsById.value[props.ch.clip] || null : null));

const behind = computed(() => Math.max(0, newestNs.value - props.ch.position_ns));
const marked = computed(() => (props.ch.has_in && props.ch.has_out && props.ch.out_ns > props.ch.in_ns ? props.ch.out_ns - props.ch.in_ns : null));
const toast = computed(() => state.toasts[props.ch.index]);

// The fader shows what it sent while it moves (the status follows at 10 Hz), then the channel's target speed.
const local = ref<number | null>(null);
let localTimer: number | undefined;
const faderValue = computed(() => (local.value != null ? local.value : props.ch.live ? 1 : Math.abs(props.ch.target_speed)));
function onFader(v: number) {
  local.value = v;
  clearTimeout(localTimer);
  localTimer = window.setTimeout(() => (local.value = null), 600);
  faderSpeed(props.ch.index, v);
}
function preset(v: number) {
  local.value = null;
  setSpeed(props.ch.index, v);
}

// The channel's mode, said in one word in the header and as the monitor's inner frame (docs/concepts/cued-state.md).
const mode = computed(() => {
  const c = props.ch;
  if (c.live) return "live";
  if (c.clip) return c.shot === "cued" ? "cued" : c.playing ? "play" : c.shot === "ended" ? "end" : "paused";
  return c.playing ? "play" : "free";
});
const MODE_WORD: Record<string, string> = { live: "LIVE", cued: "CUED", play: "PLAY", paused: "PAUSED", end: "END", free: "BUFFER" };
// What IN and OUT do now: trim the loaded clip, or (OUT after an IN) make a new one.
const trims = computed(() => editedClip(props.ch));
const outLabel = computed(() => (trims.value ? "trim" : props.ch.has_in && !props.ch.live ? "new" : "out"));
// The loaded clip on the OSD: its name, the time from IN and to OUT (source time; "+" once past OUT). The marks are
// the clip's IN and OUT while it is loaded (a trim moves them).
const secs = (ns: number) => `${(Math.abs(ns) / 1e9).toFixed(2)} s`;
const rel = computed(() => {
  const c = loaded.value;
  const ch = props.ch;
  if (!c) return null;
  const inNs = ch.has_in ? ch.in_ns : c.in_ns;
  const outNs = ch.has_out ? ch.out_ns : c.out_ns;
  const pos = smoothPos(ch);
  const fromIn = pos - inNs;
  const toOut = outNs - pos;
  return {
    name: c.name,
    fromIn: `${fromIn < 0 ? "−" : "+"}${secs(fromIn)}`,
    toOut: `${toOut < 0 ? "+" : "−"}${secs(toOut)}`,
    past: toOut < 0,
    soon: playing.value && ch.clip_remaining_ns != null && ch.clip_remaining_ns < 5e9,
  };
});

// Save: the loaded clip's or entry's speed differs from the fader's (an LSM's lever, then ENTER).
const loaded = computed(() => loadedClip(props.ch));
const entry = computed(() => channelEntry(props.ch));
const storedSpeed = computed(() => (props.ch.playlist ? (entry.value?.speed ?? null) : (loaded.value?.speed ?? null)));
const canSave = computed(
  () => !!loaded.value && !props.ch.live && storedSpeed.value != null && props.ch.target_speed > 0 && Math.abs(Math.abs(props.ch.target_speed) - storedSpeed.value) > 0.004,
);

function wheel(e: WheelEvent) {
  const d = e.deltaY || e.deltaX;
  if (!d) return;
  if (e.shiftKey) scrubSeconds(props.ch.index, d > 0 ? 1 : -1);
  else scrubFrames(props.ch.index, d > 0 ? 1 : -1);
}
</script>

<template>
  <div class="ch" :class="{ active, onair: onAir(ch) }">
    <div class="ch-head" :title="`Take control (${letter(ch)})`" @click="takeControl(ch.index)">
      <span class="letter">{{ letter(ch) }}</span><span class="ch-name">{{ ch.label }}</span>
      <span class="mode" :class="mode">{{ MODE_WORD[mode] }}</span>
      <span class="what">
        <template v-if="playlist && clip"><b>{{ playlist.name }}</b> {{ ch.playlist_index + 1 }}/{{ playlist.entries }} · {{ clip.name }}</template>
        <template v-else-if="channelClip(ch)"><b>{{ channelClip(ch)!.name }}</b></template>
        <template v-else-if="ch.live">following live</template>
        <template v-else>{{ fmtDur(behind) }} behind live</template>
      </span>
      <button v-if="ch.clip && !ch.live" class="b sm uncue" title="Uncue: let go of the clip, stay here (marks then no longer change it)" @click.stop="uncue(ch.index)">
        <Icon name="eject" />Uncue
      </button>
    </div>
    <div class="mon" :class="`m-${mode}`" @click="takeControl(ch.index)" @wheel.prevent="wheel">
      <Picture :src="`/api/v1/channels/${ch.index}/preview.jpg`" :tile="`ch${ch.index}`" :interval="200" :bump="state.previewBump" :alt="ch.label" />
      <div class="angle">{{ ch.camera }} · {{ cameraLabel(ch.camera) }}</div>
      <div v-if="ch.black" class="black">BLACK</div>
      <div class="osd">
        <span class="rel">
          <template v-if="rel">
            <span class="nm">{{ rel.name }}</span>
            <span class="tc">IN {{ rel.fromIn }}</span>
            <span class="tc" :class="{ red: rel.soon, past: rel.past }">OUT {{ rel.toOut }}</span>
          </template>
          <template v-else>&nbsp;</template>
        </span>
        <span class="speed tc">{{ playing ? fmtSpeed(ch.speed) : "\u00a0" }}</span>
      </div>
      <Transition name="toast">
        <div v-if="toast" :key="toast.seq" class="toast" :class="{ warn: toast.warn }">{{ toast.text }}</div>
      </Transition>
    </div>
    <div class="pb">
      <BufferTimeline :ch="ch" />
      <div class="row">
      <div>
        <div class="btns">
          <button class="b" :class="{ trim: trims }" :title="trims ? `Trim IN of ${trims.name} (I)` : 'Mark IN (I)'" @click="mark(ch.index, 'in')">
            <span class="lbls"><span :class="{ cur: !trims }">IN</span><span :class="{ cur: trims }">Trim IN</span></span> <kbd>I</kbd>
          </button>
          <button class="b" :class="{ trim: trims }" :title="trims ? `Trim OUT of ${trims.name} (O)` : ch.has_in ? 'Mark OUT: makes the clip and cues it (O)' : 'Mark OUT (O)'" @click="mark(ch.index, 'out')">
            <span class="lbls"><span :class="{ cur: outLabel === 'out' }">OUT</span><span :class="{ cur: outLabel === 'trim' }">Trim OUT</span><span :class="{ cur: outLabel === 'new' }">OUT · new clip</span></span> <kbd>O</kbd>
          </button>
          <button class="b" title="Go to IN ([)" aria-label="Go to IN" @click="mark(ch.index, 'goto-in')"><Icon name="goin" /></button>
          <button class="b" title="Go to OUT (])" aria-label="Go to OUT" @click="mark(ch.index, 'goto-out')"><Icon name="goout" /></button>
        </div>
        <div class="marks">
          IN <b class="tc">{{ ch.has_in ? ch.in_tc : "--:--:--:--" }}</b> OUT <b class="tc">{{ ch.has_out ? ch.out_tc : "--:--:--:--" }}</b>
          <template v-if="marked != null"> · <b class="tc">{{ fmtDur(marked) }}</b></template>
        </div>
      </div>
      <div class="right">
        <div class="btns">
          <button class="b" title="Back 5 s" aria-label="Back 5 seconds" @click="scrubSeconds(ch.index, -5)"><Icon name="back" /></button>
          <button class="b play" :class="{ on: playing }" :title="playing ? 'Pause (Space)' : 'Play (Space)'" :aria-label="playing ? 'Pause' : 'Play'" @click="playPause(ch.index)">
            <Icon :name="playing ? 'pause' : 'play'" fill />
          </button>
          <button class="b livebtn" :class="{ on: ch.live }" title="Back to live (L)" @click="transport(ch.index, 'live')">LIVE <kbd>L</kbd></button>
        </div>
        <span class="tcode tc" title="The channel's timecode">{{ smoothTimecode(ch) }}</span>
      </div>
      </div>
      <div class="speedrow">
        <SpeedFader :value="faderValue" :max="state.faderMax" :dim="ch.live && local == null" @input="onFader" />
        <span class="readout tc" :class="{ dim: ch.live && local == null }">{{ fmtSpeed(faderValue) }}</span>
        <div class="speeds">
          <button v-for="s in SPEEDS" :key="s" class="b" :class="{ on: !ch.live && Math.abs(Math.abs(ch.target_speed) - s) < 0.005 }" @click="preset(s)">
            {{ Math.round(s * 100) }}
          </button>
        </div>
        <button class="b save" :class="{ off: !canSave }" :disabled="!canSave" :tabindex="canSave ? 0 : -1" :aria-hidden="!canSave" :title="ch.playlist ? 'Store this speed as the entry\'s speed' : 'Store this speed as the clip\'s speed'" @click="saveSpeed(ch.index)">
          Save {{ fmtSpeed(ch.target_speed) }} to {{ ch.playlist ? "entry" : "clip" }}
        </button>
      </div>
    </div>
  </div>
</template>

<style scoped>
.ch { display: grid; grid-template-rows: auto auto 1fr; background: var(--panel); border: 3px solid var(--border); border-radius: 8px; overflow: hidden; min-height: 0; }
.ch.onair { border-color: var(--tally); }
.ch-head { display: flex; align-items: center; gap: 10px; height: 38px; padding: 0 10px; background: var(--panel2); font-size: 14px; cursor: pointer; transition: background-color .15s ease-out; min-width: 0; }
.ch.active .ch-head { background: var(--accent-tint); }
.letter { display: inline-grid; place-items: center; width: 24px; height: 24px; border-radius: 4px; font-weight: 800; font-size: 14px; border: 1px solid var(--border2); color: var(--muted); flex: none; }
.ch.active .letter { background: var(--accent); border-color: var(--accent); color: #031426; }
.ch-name { font-weight: 700; font-size: 16px; white-space: nowrap; }
@media (pointer: coarse) { .pb :deep(.b) { height: 36px; } .pb kbd { display: none; } }
.what { color: var(--muted); font-size: 13px; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; min-width: 0; }
.what b { color: var(--text); font-weight: 600; }
.uncue { margin-left: auto; flex: none; }
/* The mode word: where the operator looks for the state, in the state's colour. */
.mode { flex: none; box-sizing: border-box; min-width: 62px; text-align: center; font: 800 11px var(--mono); letter-spacing: .06em; padding: 2px 7px; border-radius: 3px; border: 1px solid var(--border2); color: var(--muted); }
.mode.live { color: var(--text); }
.mode.cued { background: var(--cue); border-color: var(--cue); color: #1d1600; }
.mode.play { background: var(--ok); border-color: var(--ok); color: #06140b; }
.mode.paused, .mode.end { border-color: var(--cue); color: var(--cue); }
/* The inner frame: the state from across the desk; the tally stays the outer border. */
.mon::after { content: ""; position: absolute; inset: 0; border: 4px solid transparent; pointer-events: none; transition: border-color .15s ease-out; }
.mon.m-cued::after { border-color: var(--cue); }
.mon.m-play::after { border-color: rgba(63,185,104,.7); }
.mon.m-paused::after, .mon.m-end::after { border-color: rgba(232,195,58,.45); }
.b.trim { border-color: var(--cue); color: var(--cue); }
/* No layout shifts: a control that changes its words keeps the width of its longest one, one that comes and goes keeps
   its place. */
.lbls { display: inline-grid; justify-items: center; }
.lbls > span { grid-area: 1 / 1; visibility: hidden; }
.lbls > span.cur { visibility: visible; }
.b.save { width: 150px; flex: none; overflow: hidden; text-overflow: ellipsis; }
.b.save.off { visibility: hidden; }
.ch.active .what { color: var(--muted-on-tint); }
/* The monitor keeps 16:9 by its width, at most about half the window high; the OSD scales with it. */
.mon { position: relative; background: #000; min-height: 0; cursor: pointer; aspect-ratio: 16 / 9; width: 100%; max-height: 52dvh; container-type: inline-size; }
.angle { position: absolute; left: 12px; top: 8px; font-weight: 700; font-size: clamp(12px, 2cqi, 15px); color: #fff; text-shadow: 0 1px 2px #000, 0 0 6px rgba(0,0,0,.8); pointer-events: none; }
.black { position: absolute; inset: 0; display: grid; place-items: center; font-weight: 800; letter-spacing: .1em; color: var(--muted); }
.osd { position: absolute; left: 0; right: 0; bottom: 0; display: flex; justify-content: space-between; align-items: flex-end; gap: 12px; padding: 6px 12px; pointer-events: none; text-shadow: 0 1px 2px #000, 0 0 6px rgba(0,0,0,.8); }
.osd .speed { font-size: clamp(14px, 2.6cqi, 20px); font-weight: 700; opacity: .85; line-height: 1.2; height: 1.2em; min-width: 4.5ch; text-align: right; flex: none; overflow: hidden; }
/* The loaded clip: small and half transparent, so the picture stays the subject. */
.rel { display: flex; gap: 12px; align-items: baseline; line-height: 1.2; min-width: 0; font-size: clamp(11px, 1.8cqi, 14px); font-weight: 600; opacity: .72; white-space: nowrap; }
.rel .nm { overflow: hidden; text-overflow: ellipsis; min-width: 0; }
.rel .past { color: var(--warn); opacity: 1; }
.rel .red { color: #ff6a60; opacity: 1; }
.toast { position: absolute; left: 50%; bottom: 18%; transform: translateX(-50%); background: rgba(18,25,34,.94); border: 1px solid var(--border2); padding: 7px 14px; border-radius: 6px; font-size: 14px; pointer-events: none; max-width: 90%; text-align: center; }
.toast.warn { border-color: var(--warn); color: var(--warn); }
.toast-enter-active, .toast-leave-active { transition: opacity .18s ease-out, transform .18s cubic-bezier(.2,.8,.2,1); }
.toast-enter-from, .toast-leave-to { opacity: 0; transform: translate(-50%, 6px); }
/* Timeline on top; marking on the left, transport and speeds on the right, wrapping under each other when narrow. */
.pb { display: flex; flex-direction: column; gap: 6px; padding: 8px 10px; border-top: 1px solid var(--border); }
.row { display: flex; flex-wrap: wrap; justify-content: space-between; align-items: flex-start; gap: 6px 10px; }
.right { display: flex; flex-direction: column; gap: 4px; align-items: flex-end; }
.tcode { font-size: 15px; font-weight: 600; letter-spacing: .02em; }
.btns, .speeds { flex-wrap: wrap; }
.speedrow { display: flex; align-items: center; flex-wrap: wrap; gap: 6px 10px; }
.speedrow .fader { flex: 1 1 200px; }
.readout { font-size: 16px; font-weight: 700; min-width: 52px; text-align: right; }
.readout.dim { color: var(--muted); }
.speeds { display: flex; gap: 3px; }
.speeds .b { min-width: 0; padding: 0 6px; }
.b.save { border-color: var(--accent); color: var(--accent); }
.b.sm { height: 26px; font-size: 12px; padding: 0 8px; min-width: 0; }
.b.ghost { background: transparent; }
.marks { font-size: 12px; color: var(--muted); display: flex; gap: 6px; margin-top: 5px; }
.marks b { color: var(--text); font-weight: 500; }
@media (prefers-reduced-motion: reduce) { .toast-enter-active, .toast-leave-active { transition: none; } }
</style>
