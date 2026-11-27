// The synced camera pictures (operators, SPECIFICATION.md §8.9): every camera's stored frame at the operator's
// position, from the status's `multiview.cameras`. A frame is fetched once per (camera, frame_ns) and the row is
// swapped together, when every picture of a round has loaded. At most one round at a time, about 5 a second.
import { onUnmounted, ref, watch, type Ref } from "vue";
import type { MultiviewCamera } from "./api";

export type SyncedFrame = { url: string; tc: string | null } | { missing: true };

export function useSyncedFrames(source: () => MultiviewCamera[] | null): Ref<Record<number, SyncedFrame> | null> {
  const shown = ref<Record<number, SyncedFrame> | null>(null);
  const cache = new Map<string, string>();
  let busy = false;
  let again = false;
  let last = 0;
  let timer: number | undefined;
  let gone = false;
  const key = (t: MultiviewCamera) => `${t.camera}:${t.frame_ns}`;

  async function fetchOne(t: MultiviewCamera) {
    const k = key(t);
    if (cache.has(k)) return;
    try {
      const resp = await fetch(`/api/v1/cameras/${t.camera}/preview.jpg?tai_ns=${t.frame_ns}`, { cache: "no-store" });
      if (!resp.ok) return;
      const url = URL.createObjectURL(await resp.blob());
      if (gone) URL.revokeObjectURL(url);
      else cache.set(k, url);
    } catch {
      /* missing this round */
    }
  }

  function prune(keep: Set<string>) {
    const limit = Math.max(32, keep.size * 4);
    for (const [k, url] of cache) {
      if (cache.size <= limit) break;
      if (keep.has(k)) continue;
      URL.revokeObjectURL(url);
      cache.delete(k);
    }
  }

  async function round() {
    timer = undefined;
    const target = source();
    if (!target) {
      shown.value = null;
      return;
    }
    if (busy) {
      again = true;
      return;
    }
    if (document.hidden) {
      timer = window.setTimeout(round, 500);
      return;
    }
    const wait = last + 200 - Date.now();
    if (wait > 0) {
      timer = window.setTimeout(round, wait);
      return;
    }
    busy = true;
    last = Date.now();
    try {
      await Promise.all(target.filter((t) => t.frame_ns != null).map(fetchOne));
      if (gone || !source()) return;
      const next: Record<number, SyncedFrame> = {};
      const keep = new Set<string>();
      for (const t of target) {
        const url = t.frame_ns != null ? cache.get(key(t)) : undefined;
        if (url) keep.add(key(t));
        next[t.camera] = url ? { url, tc: t.tc } : { missing: true };
      }
      shown.value = next;
      prune(keep);
    } finally {
      busy = false;
      if (again && !gone) {
        again = false;
        timer = window.setTimeout(round, 0);
      }
    }
  }

  watch(
    () => source()?.map(key).join(",") ?? "",
    () => {
      clearTimeout(timer);
      round();
    },
    { immediate: true },
  );
  onUnmounted(() => {
    gone = true;
    clearTimeout(timer);
    for (const url of cache.values()) URL.revokeObjectURL(url);
    cache.clear();
  });
  return shown;
}
