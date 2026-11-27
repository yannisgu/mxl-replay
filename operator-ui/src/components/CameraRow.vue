<script setup lang="ts">
// Every camera, small. With operators on the server (JPEG previews) the tiles are the synced pick: each camera's
// frame at the position of the channel under control. Otherwise they are the live pictures. A click puts the angle
// on the channel under control.
import { computed } from "vue";
import { fmtBytes } from "../lib/api";
import { connectXTouch, disconnectXTouch, xtouch } from "../lib/xtouch";
import { cameras, control, editedClip, letter, myChannels, newestNs, oldestNs, onAir, operator, setAngle, setFaderMax, state } from "../lib/store";
import { useSyncedFrames } from "../lib/synced";
import Icon from "./Icon.vue";
import { smoothTimecode } from "../lib/smooth";
import Picture from "./Picture.vue";

defineEmits<{ keys: []; fullscreen: [] }>();

const synced = computed(() => {
  const mv = operator.value?.multiview;
  const c = control.value;
  if (state.status?.preview?.mode === "webrtc" || !mv || !c || mv.follow !== c.index || mv.live) return null;
  return mv.cameras;
});
const frames = useSyncedFrames(() => synced.value);
const frameOf = (cam: number) => {
  const f = frames.value?.[cam];
  return f && "url" in f ? f : null;
};

// With a clip loaded on the channel under control, a tile changes the clip's angle.
const clipOnControl = computed(() => (control.value ? editedClip(control.value) : null));
const recording = computed(() => cameras.value.filter((c) => c.record && c.recording).length);
const recordable = computed(() => cameras.value.filter((c) => c.record).length);
const bufferMin = computed(() => Math.round((newestNs.value - oldestNs.value) / 60e9));
const onCam = (cam: number) => myChannels.value.filter((c) => c.camera === cam);
</script>

<template>
  <section id="cams">
    <div class="info">
      <div class="sync"><template v-if="clipOnControl">Angle of <span class="clip">{{ clipOnControl.name }}</span></template><template v-else-if="synced">Angles at <span>{{ control?.label }}</span></template><template v-else>Live angles</template></div>
      <div class="tc big">{{ smoothTimecode(control) }}</div>
      <div class="health">
        <span :class="{ bad: recording < recordable }"><span class="dot"></span>REC {{ recording }}/{{ recordable }}</span>
        <span>Buffer {{ bufferMin }} min</span>
        <span v-if="state.status">Free {{ fmtBytes(state.status.free_bytes) }}</span>
        <span v-if="!state.connected" class="bad">Reconnecting</span>
      </div>
      <div class="tools">
        <button class="b" title="Keys (?)" @click="$emit('keys')"><Icon name="keys" />Keys</button>
        <button
          v-if="xtouch.supported"
          class="b"
          :class="{ on: !!xtouch.device }"
          :title="xtouch.device ? `${xtouch.device} connected: click to disconnect` : 'Connect a Behringer X-Touch One (Web MIDI, Mackie Control mode)'"
          @click="xtouch.device ? disconnectXTouch() : connectXTouch()"
        >X-Touch</button>
        <button class="b" :class="{ on: state.faderMax === 2 }" title="Speed faders up to 200 % (this browser)" @click="setFaderMax(state.faderMax === 2 ? 1 : 2)">200%</button>
        <button class="b" title="Full screen (F)" aria-label="Full screen" @click="$emit('fullscreen')"><Icon name="full" /></button>
      </div>
    </div>
    <div class="tiles">
    <button v-for="cam in cameras" :key="cam.index" class="cam" :class="{ sel: control?.camera === cam.index }" :title="clipOnControl ? `Put ${clipOnControl.name} on angle ${cam.index} (key ${cam.index})` : `Angle ${cam.index} on ${control?.label} (key ${cam.index})`" @click="control && setAngle(control.index, cam.index)">
      <div class="pic">
        <template v-if="synced && frames">
          <img v-if="frameOf(cam.index)" :src="frameOf(cam.index)!.url" :alt="cam.label" />
          <span v-else class="noframe">NO FRAME</span>
        </template>
        <Picture v-else :src="`/api/v1/cameras/${cam.index}/preview.jpg`" :tile="`cam${cam.index}`" :interval="500" :alt="cam.label" />
        <span class="tag">
          <b v-for="c in onCam(cam.index)" :key="c.index" :class="{ act: control?.index === c.index, air: onAir(c) }">{{ letter(c) }}</b>
        </span>
      </div>
      <div class="lbl"><span class="num">{{ cam.index }}</span><span class="name">{{ cam.label }}</span><span v-if="cam.record && !cam.recording" class="nosig">NO SIGNAL</span></div>
    </button>
    </div>
  </section>
</template>

<style scoped>
/* Info on the left, the tiles in one row that scrolls sideways when they do not fit; on small screens the info goes on top. */
#cams { display: flex; gap: 8px; background: var(--panel); border: 1px solid var(--border); border-radius: 8px; padding: 8px; min-height: 0; min-width: 0; }
.info { display: flex; flex-direction: column; gap: 4px; font-size: 12px; color: var(--muted); padding: 2px 2px 0 4px; min-width: 0; flex: 0 0 266px; }
.tiles { display: flex; gap: 8px; overflow-x: auto; min-width: 0; flex: 1; padding-bottom: 2px; }
.tiles .cam { flex: 0 0 clamp(120px, 9.5vw, 176px); }
@media (max-width: 759px) {
  #cams { flex-direction: column; padding: 6px; }
  .info { flex: none; flex-direction: row; flex-wrap: wrap; align-items: center; gap: 4px 12px; }
  .health { margin-top: 0; }
  .tools { margin: 0 0 0 auto; }
  .tiles .cam { flex-basis: 132px; }
}
.sync { color: var(--text); font-size: 13px; font-weight: 600; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
.sync span { color: var(--accent); }
.sync span.clip { color: var(--cue); }
.big { color: var(--text); font-size: 16px; }
.health { display: flex; flex-wrap: wrap; gap: 2px 10px; margin-top: auto; font-size: 11px; }
.health .dot { display: inline-block; width: 7px; height: 7px; border-radius: 50%; background: var(--ok); margin-right: 4px; vertical-align: 1px; }
.health .bad { color: var(--warn); }
.health .bad .dot { background: var(--warn); }
.tools { display: flex; flex-wrap: wrap; gap: 4px; margin-top: 4px; }
.tools .b { height: 26px; font-size: 12px; padding: 0 8px; }
.cam { position: relative; border-radius: 5px; overflow: hidden; border: 2px solid transparent; background: #000; padding: 0; display: flex; flex-direction: column; align-self: start; text-align: left; transition: border-color .12s; min-width: 0; }
.cam:hover { border-color: var(--border2); }
.cam.sel { border-color: var(--accent); }
.pic { position: relative; width: 100%; aspect-ratio: 16 / 9; display: grid; place-items: center; }
.pic img { width: 100%; height: 100%; object-fit: cover; display: block; }
.noframe { color: var(--muted); font-size: 11px; font-weight: 700; }
.lbl { display: flex; align-items: center; gap: 6px; font-size: 13px; padding: 3px 6px; background: var(--panel2); min-width: 0; }
.lbl .num { font: 700 12px var(--mono); background: var(--chip); border: 1px solid var(--border2); border-radius: 3px; padding: 0 5px; }
.lbl .name { white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
.lbl .nosig { margin-left: auto; font-size: 10px; font-weight: 700; color: var(--warn); }
.tag { position: absolute; left: 5px; top: 5px; display: flex; gap: 3px; }
.tag b { font-size: 11px; font-weight: 800; width: 18px; height: 18px; display: grid; place-items: center; border-radius: 3px; background: rgba(0,0,0,.66); color: #fff; border: 1px solid rgba(255,255,255,.5); }
.tag b.act { background: var(--accent); border-color: var(--accent); color: #031426; }
.tag b.air { box-shadow: 0 0 0 2px var(--tally); }
</style>
