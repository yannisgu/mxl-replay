<script setup lang="ts">
// A preview picture. JPEG mode: fetched every `interval` ms while the page is visible (one request at a time) and at
// once when `bump` changes. WebRTC mode: tile `tile` ("ch1", "cam2") of the page's one mosaic stream.
import { computed, onMounted, onUnmounted, ref, watch } from "vue";
import { preview, startPreview, tileCrop } from "../lib/preview";
import { state } from "../lib/store";

const props = withDefaults(defineProps<{ src: string; tile: string; interval?: number; bump?: number; alt?: string }>(), {
  interval: 1000,
  bump: 0,
  alt: "",
});

const webrtc = computed(() => state.status?.preview?.mode === "webrtc");
const crop = computed(() => (webrtc.value ? tileCrop(props.tile) : null));
const video = ref<HTMLVideoElement | null>(null);
watch(webrtc, (on) => on && startPreview(), { immediate: true });
watch([video, () => preview.stream], ([el, stream]) => {
  if (el && stream && el.srcObject !== stream) {
    el.srcObject = stream;
    el.play().catch(() => {});
  }
});

const url = ref("");
const missing = ref(false);
let busy = false;
let timer: number | undefined;

async function load() {
  if (busy || document.hidden || !state.status || webrtc.value) return;
  busy = true;
  try {
    const resp = await fetch(`${props.src}?t=${Date.now()}`, { cache: "no-store" });
    if (resp.ok) {
      const next = URL.createObjectURL(await resp.blob());
      if (url.value) URL.revokeObjectURL(url.value);
      url.value = next;
      missing.value = false;
    } else {
      missing.value = true;
    }
  } catch {
    missing.value = true;
  } finally {
    busy = false;
  }
}

watch(
  () => [props.src, props.bump, !!state.status],
  () => {
    busy = false;
    load();
  },
);
onMounted(() => {
  load();
  timer = window.setInterval(load, props.interval);
});
onUnmounted(() => {
  clearInterval(timer);
  if (url.value) URL.revokeObjectURL(url.value);
});
</script>

<template>
  <div class="picture">
    <div v-if="webrtc" class="crop" :style="crop?.box">
      <video ref="video" :style="crop?.video" muted autoplay playsinline :aria-label="alt"></video>
      <span v-if="!preview.connected" class="none">Connecting…</span>
    </div>
    <img v-else-if="url && !missing" :src="url" :alt="alt" />
    <span v-else class="none">No picture</span>
  </div>
</template>

<style scoped>
/* The picture keeps its aspect ratio inside whatever box it is given (container units). */
.picture { position: relative; width: 100%; height: 100%; display: grid; place-items: center; background: #000; overflow: hidden; container-type: size; }
.picture img { width: 100%; height: 100%; object-fit: contain; display: block; }
.crop { --ar: 1.7778; position: relative; width: min(100cqw, calc(100cqh * var(--ar))); height: min(100cqh, calc(100cqw / var(--ar))); overflow: hidden; }
.crop video { position: absolute; left: 0; top: 0; width: 100%; height: 100%; display: block; }
.none { position: absolute; inset: 0; display: grid; place-items: center; color: var(--muted); font-size: 12px; }
</style>
