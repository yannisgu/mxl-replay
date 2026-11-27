// The operator screen's state: the status pushed on the WebSocket (applied at most 10 times a second), the clips
// and playlists, the page's operator and the channel under control, and the actions.
//
// The page is one operator (?operator=N, default 1). With operators on the server (SPECIFICATION.md §8.9) the
// channels are the operator's and control is the server's (POST /api/v1/operators/{o}/control); on a server
// without operators every channel is shown and control is kept in this browser.
import { computed, reactive } from "vue";
import { api, ApiError, enc, fmtSpeed, serial, tagsOf, type Channel, type Clip, type TagUse, type Operator, type Playlist, type PlaylistEntry, type PlaylistSummary, type Status } from "./api";

const LOCAL_CONTROL_KEY = "mxl-operator.control";
const PLAYLIST_KEY = "mxl-operator.playlist";
const FADER_KEY = "mxl-operator.fader-max";

function stored(key: string): string | null {
  try {
    return localStorage.getItem(key);
  } catch {
    return null;
  }
}
function store(key: string, value: string) {
  try {
    localStorage.setItem(key, value);
  } catch {
    /* kept for this tab only */
  }
}

interface Toast {
  text: string;
  warn: boolean;
  seq: number;
}

export const state = reactive({
  status: null as Status | null,
  /** performance.now() when the status was applied: timers carry the position on from there (lib/smooth.ts). */
  statusAt: 0,
  clips: [] as Clip[],
  playlists: [] as PlaylistSummary[],
  /** The open playlist's entries (GET /api/v1/playlists/{id}). */
  playlist: null as Playlist | null,
  playlistId: stored(PLAYLIST_KEY) || "",
  connected: false,
  indexing: false,
  statusError: "",
  operator: Number(new URLSearchParams(location.search).get("operator")) || 1,
  localControl: Number(stored(LOCAL_CONTROL_KEY)) || 0,
  /** The control the page asked for and has no answer for yet. */
  wantedControl: 0,
  selectedClip: "",
  /** Feedback on the monitor of the channel it concerns. */
  toasts: {} as Record<number, Toast>,
  previewBump: 0,
  /** The speed fader's top: 1 (100 %, the default) or 2 (200 %). A setting of this browser. */
  faderMax: stored(FADER_KEY) === "2" ? 2 : 1,
  /** The open speed presets: for a clip, a playlist entry or every entry of the open playlist. */
  speedMenu: null as null | { kind: "clip" | "entry" | "all"; id: string; index: number; x: number; y: number },
  /** Every tag of the shared list or of a clip, most used first. */
  tags: [] as TagUse[],
  /** The open ⋯ menu of a clip. */
  clipMenu: null as null | { id: string; x: number; y: number },
});

export function setFaderMax(max: 1 | 2) {
  state.faderMax = max;
  store(FADER_KEY, String(max));
}

export const operator = computed<Operator | null>(() => {
  const ops = state.status?.operators;
  if (!ops?.length) return null;
  return ops.find((o) => o.index === state.operator) || ops[0];
});

/** The operator's channels in its order, or every channel. */
export const myChannels = computed<Channel[]>(() => {
  const all = state.status?.channels || [];
  const op = operator.value;
  if (!op) return all;
  return op.channels.map((i) => all.find((c) => c.index === i)).filter((c): c is Channel => !!c);
});

export const cameras = computed(() => state.status?.cameras || []);

export const controlIndex = computed(() => {
  const list = myChannels.value;
  const want = state.wantedControl || operator.value?.primary || state.localControl;
  return list.some((c) => c.index === want) ? want : list[0]?.index || 0;
});
export const control = computed(() => myChannels.value.find((c) => c.index === controlIndex.value) || null);

/** The channel's letter on this screen: A, B, … in the operator's order (the A/B keys). */
export const letter = (ch: Channel | number) => {
  const index = typeof ch === "number" ? ch : ch.index;
  const at = myChannels.value.findIndex((c) => c.index === index);
  return at >= 0 ? String.fromCharCode(65 + at) : "?";
};

export const clipsById = computed(() => Object.fromEntries(state.clips.map((c) => [c.id, c])) as Record<string, Clip>);
export const cameraLabel = (index: number) => (index === 0 ? "Upload" : cameras.value.find((c) => c.index === index)?.label || `Cam ${index}`);

/** The newest recorded frame: "live" on the timelines. */
export const newestNs = computed(() => Math.max(0, ...cameras.value.map((c) => c.newest_ns || 0)) || state.status?.tai_ns || 0);
export const oldestNs = computed(() => {
  const list = cameras.value.filter((c) => c.oldest_ns > 0).map((c) => c.oldest_ns);
  return list.length ? Math.min(...list) : newestNs.value;
});

export const onAir = (ch: Channel) => ch.tally === 1;

/** The open playlist's entry a channel is on, if the channel plays the open playlist. */
export const channelEntry = (ch: Channel) =>
  ch.playlist && !ch.live && state.playlist?.id === ch.playlist ? state.playlist.entries[ch.playlist_index] || null : null;

/** The clip loaded on a channel (a clip, or the clip of its playlist entry): its IN/OUT are the channel's marks
 *  when it is cued. For an entry of the open playlist it is the entry's clip (a copy made for it, too). */
export function loadedClip(ch: Channel) {
  if (ch.live || !ch.clip) return null;
  const e = channelEntry(ch);
  return clipsById.value[e?.clip_id || ch.clip] || null;
}

/** The loaded clip when the channel's marks differ from its IN/OUT (by more than half a frame). */
export function marksChanged(ch: Channel) {
  const c = loadedClip(ch);
  if (!c || !ch.has_in || !ch.has_out || ch.out_ns <= ch.in_ns) return null;
  const tol = (state.status?.frame_ns || 20e6) / 2;
  return Math.abs(ch.in_ns - c.in_ns) > tol || Math.abs(ch.out_ns - c.out_ns) > tol ? c : null;
}

/** The clip a channel shows as a single clip (not as a playlist entry). */
export const channelClip = (ch: Channel) => (ch.clip && !ch.playlist ? clipsById.value[ch.clip] || null : null);

// ------------------------------------------------------------------ feedback

let toastSeq = 0;
const toastTimers: Record<number, number> = {};
export function toast(channel: number, text: string, warn = false) {
  state.toasts[channel] = { text, warn, seq: ++toastSeq };
  clearTimeout(toastTimers[channel]);
  toastTimers[channel] = window.setTimeout(() => delete state.toasts[channel], warn ? 3000 : 1800);
}

/** Runs an action; a failure is shown on the channel's monitor. */
async function act<T>(channel: number, fn: () => Promise<T>): Promise<T | undefined> {
  try {
    return await fn();
  } catch (e) {
    toast(channel, (e as Error).message, true);
    return undefined;
  }
}

// ------------------------------------------------------------------ status

let answeredNs = 0;

function applyStatus(status: Status) {
  // A push made before the last control answer would show the old control for a moment.
  if (status.tai_ns < answeredNs && state.status) return;
  state.status = status;
  state.statusAt = performance.now();
  state.indexing = false;
  state.statusError = "";
}

async function refreshStatus() {
  try {
    applyStatus(await api.get<Status>("/api/v1/status"));
  } catch (e) {
    if (e instanceof ApiError && e.status === 503 && e.body?.status === "indexing") state.indexing = true;
    else state.statusError = (e as Error).message;
  }
}

let listsSignature = "";
export async function refreshLists() {
  const [clips, playlists, tags] = await Promise.allSettled([
    api.get<Clip[]>("/api/v1/clips"),
    api.get<PlaylistSummary[]>("/api/v1/playlists"),
    api.get<{ tags: TagUse[] }>("/api/v1/tags"),
  ]);
  if (tags.status === "fulfilled" && JSON.stringify(tags.value.tags) !== JSON.stringify(state.tags)) state.tags = tags.value.tags;
  if (clips.status === "fulfilled") state.clips = clips.value.sort((a, b) => serial(b.id) - serial(a.id));
  if (playlists.status === "fulfilled") {
    state.playlists = playlists.value.sort((a, b) => serial(a.id) - serial(b.id));
    if (!state.playlists.some((p) => p.id === state.playlistId)) state.playlistId = state.playlists[0]?.id || "";
    // The open playlist again when another UI changed it.
    const p = state.playlists.find((x) => x.id === state.playlistId);
    const sig = p ? `${p.id} ${p.name} ${p.loop} ${p.clip_ids.join(",")} ${p.duration_ns}` : "";
    if (sig !== listsSignature || state.playlist?.id !== state.playlistId) {
      listsSignature = sig;
      await loadPlaylist();
    }
  }
}

async function loadPlaylist() {
  const id = state.playlistId;
  if (!id) {
    state.playlist = null;
    return;
  }
  try {
    const pl = await api.get<Playlist>(`/api/v1/playlists/${enc(id)}`);
    if (state.playlistId === id) state.playlist = pl;
  } catch {
    /* the next refresh */
  }
}

let socket: WebSocket | null = null;
function applyPending() {
  const text = pending;
  pending = null;
  if (text == null) return;
  try {
    applyStatus(JSON.parse(text));
  } catch {
    /* the next push */
  }
}
let pending: string | null = null;
let retry = 1000;

function connect() {
  const protocol = location.protocol === "https:" ? "wss" : "ws";
  socket = new WebSocket(`${protocol}://${location.host}/api/v1/events`);
  socket.onopen = () => {
    state.connected = true;
    retry = 1000;
  };
  // Each push is applied on the next animation frame (several in one frame: the newest). The timers carry the
  // position on between pushes (lib/smooth.ts), so a push is used as soon as it is here, not on a timer of its own.
  socket.onmessage = (event) => {
    if (pending == null) requestAnimationFrame(applyPending);
    pending = event.data;
  };
  socket.onclose = () => {
    state.connected = false;
    socket = null;
    setTimeout(connect, retry);
    retry = Math.min(retry * 2, 5000);
  };
}

export function start() {
  refreshStatus();
  refreshLists();
  connect();
  setInterval(() => {
    if (!state.connected || state.indexing || !state.status) refreshStatus();
  }, 1000);
  setInterval(refreshLists, 3000);
}

// ------------------------------------------------------------------ control

let controlQueue: Promise<unknown> = Promise.resolve();

/** Takes control of a channel: the keys, the angle tiles and the clip bin act on it. */
export function takeControl(index: number) {
  if (index === controlIndex.value || !myChannels.value.some((c) => c.index === index)) return;
  const op = operator.value;
  if (!op) {
    state.localControl = index;
    store(LOCAL_CONTROL_KEY, String(index));
    return;
  }
  state.wantedControl = index;
  controlQueue = controlQueue.then(async () => {
    try {
      const status = await api.post<Status>(`/api/v1/operators/${op.index}/control`, { channels: [index] });
      answeredNs = Math.max(answeredNs, status.tai_ns);
      state.status = status;
    } catch (e) {
      toast(index, (e as Error).message, true);
    } finally {
      if (state.wantedControl === index) state.wantedControl = 0;
    }
  });
}

export function nextControl() {
  const list = myChannels.value;
  if (list.length < 2) return;
  const at = list.findIndex((c) => c.index === controlIndex.value);
  takeControl(list[(at + 1) % list.length].index);
}

// ------------------------------------------------------------------ channel actions

function channelPost(channel: number, action: string, body: unknown) {
  return act(channel, async () => {
    applyStatus(await api.post<Status>(`/api/v1/channels/${channel}/${action}`, body));
  });
}

const chan = (index: number) => state.status?.channels.find((c) => c.index === index) || null;

export const transport = (channel: number, command: "play" | "pause" | "live" | "stop" | "uncue") => channelPost(channel, "transport", { command });
export const setSpeed = (channel: number, speed: number) => channelPost(channel, "speed", { speed });
export const scrubSeconds = (channel: number, seconds: number) => channelPost(channel, "position", { seconds });

// The fader: while it is dragged at most 20 speeds a second, always the newest last. On a cued, paused or ended
// channel the first move also plays, as an LSM lever does.
const speedPending: Record<number, number | null> = {};
const speedBusy: Record<number, boolean> = {};
export async function faderSpeed(channel: number, speed: number) {
  const ch = chan(channel);
  if (!ch) return;
  if (!ch.playing && !ch.live && speed > 0 && speedPending[channel] == null && !speedBusy[channel]) await transport(channel, "play");
  speedPending[channel] = speed;
  if (speedBusy[channel]) return;
  speedBusy[channel] = true;
  while (speedPending[channel] != null) {
    const v = speedPending[channel]!;
    speedPending[channel] = null;
    await Promise.all([channelPost(channel, "speed", { speed: v }), new Promise((r) => setTimeout(r, 50))]);
  }
  speedBusy[channel] = false;
}

/** The fader's speed as the loaded clip's or playlist entry's speed (an LSM's lever, then ENTER). */
export async function saveSpeed(channel: number) {
  const ch = chan(channel);
  if (!ch || ch.live || !ch.clip) return;
  const speed = Math.round(Math.abs(ch.target_speed) * 100) / 100;
  if (ch.playlist) {
    const ok = await editPlaylist(ch.playlist, (entries) => {
      const e = entries[ch.playlist_index];
      if (e) e.speed = speed;
    });
    if (ok) toast(channel, `Entry ${ch.playlist_index + 1} at ${fmtSpeed(speed)}`);
  } else {
    const clip = clipsById.value[ch.clip];
    if (clip && (await patchClip(channel, clip.id, { speed }))) toast(channel, `${clip.name} at ${fmtSpeed(speed)}`);
  }
}

export function playPause(channel: number) {
  const ch = chan(channel);
  if (!ch) return;
  return transport(channel, ch.playing || ch.live ? "pause" : "play");
}

/** How far from a loaded clip a mark still edits it (the zoomed timeline's margin). Further away it is a new shot. */
const NEAR_CLIP_NS = 10e9;
/** The clip IN, OUT and a camera change edit on this channel now: the loaded clip, with the marks near it. */
export function editedClip(ch: Channel) {
  const clip = loadedClip(ch);
  return clip && ch.has_in && ch.has_out && nearClip(ch, clip) ? clip : null;
}

const nearClip = (ch: Channel, clip: Clip) =>
  ch.in_ns >= clip.in_ns - NEAR_CLIP_NS && ch.in_ns <= clip.out_ns + NEAR_CLIP_NS && ch.out_ns >= clip.in_ns - NEAR_CLIP_NS && ch.out_ns <= clip.out_ns + NEAR_CLIP_NS;

/** Marks IN or OUT, or goes there.
 *  - With a clip (or playlist entry) loaded and the marks near it, IN and OUT are the clip's: each mark is stored in
 *    the clip at once (every playlist with the clip plays the new range; Make a copy first to change one entry only).
 *  - Otherwise OUT after an IN makes the clip and cues it there (paused at its IN): IN, OUT, Space plays it. */
export async function mark(channel: number, which: "in" | "out" | "goto-in" | "goto-out") {
  const ch = chan(channel);
  if (which === "goto-in" && !ch?.has_in) return toast(channel, "No IN mark yet", true);
  if (which === "goto-out" && !ch?.has_out) return toast(channel, "No OUT mark yet", true);
  await channelPost(channel, "marks", { which });
  if (which !== "in" && which !== "out") return;
  const now = chan(channel);
  if (!now) return;
  const loaded = loadedClip(now);
  const clip = loaded && now.has_in && now.has_out && nearClip(now, loaded) ? loaded : null;
  if (!clip) {
    if (which === "out" && now.has_in) {
      if (now.out_ns <= now.in_ns) return toast(channel, "OUT is before IN: mark OUT again", true);
      const id = await createClip(channel);
      if (id) await cueClip(channel, id);
    }
    return;
  }
  if (now.out_ns <= now.in_ns) return toast(channel, "OUT must be after IN: the clip is not changed", true);
  if (!marksChanged(now)) return;
  if (await patchClip(channel, clip.id, { in_ns: now.in_ns, out_ns: now.out_ns })) {
    // Other playlists with the clip: the channel's own one only when the clip is in it twice.
    const lists = state.playlists
      .filter((p) => p.clip_ids.includes(clip.id) && (p.id !== now.playlist || p.clip_ids.filter((id) => id === clip.id).length > 1))
      .map((p) => p.name);
    toast(channel, `${which.toUpperCase()} ${which === "in" ? now.in_tc : now.out_tc} · ${clip.name}${lists.length ? ` · also in ${lists.join(", ")}` : ""}`);
  }
}

/** Puts a camera on the channel. With a clip (or playlist entry) loaded and the marks near it, the clip takes the
 *  camera too, as IN and OUT do: the same range on the other angle, in every playlist that has the clip. */
export async function setAngle(channel: number, camera: number) {
  await channelPost(channel, "angle", { camera });
  state.previewBump++;
  const ch = chan(channel);
  const clip = ch && loadedClip(ch);
  if (ch && clip && clip.camera !== 0 && clip.camera !== camera && ch.has_in && ch.has_out && nearClip(ch, clip)) {
    if (await patchClip(channel, clip.id, { camera })) {
      const lists = state.playlists
        .filter((p) => p.clip_ids.includes(clip.id) && (p.id !== ch.playlist || p.clip_ids.filter((id) => id === clip.id).length > 1))
        .map((p) => p.name);
      return toast(channel, `${clip.name} now on ${camera} · ${cameraLabel(camera)}${lists.length ? ` · also in ${lists.join(", ")}` : ""}`);
    }
    return;
  }
  toast(channel, `Angle ${camera} · ${cameraLabel(camera)}`);
}

// Frame steps (keys, the wheel) are added up while a request is on its way.
const scrubPending: Record<number, number> = {};
const scrubBusy: Record<number, boolean> = {};
export async function scrubFrames(channel: number, frames: number) {
  scrubPending[channel] = (scrubPending[channel] || 0) + frames;
  if (scrubBusy[channel]) return;
  scrubBusy[channel] = true;
  while (scrubPending[channel]) {
    const n = scrubPending[channel];
    scrubPending[channel] = 0;
    await channelPost(channel, "position", { frames: n });
  }
  scrubBusy[channel] = false;
}

// Seeks from dragging on a timeline: only the newest is sent once the one on its way is answered.
const seekPending: Record<number, number | null> = {};
const seekBusy: Record<number, boolean> = {};
export async function seek(channel: number, taiNs: number) {
  seekPending[channel] = Math.max(0, Math.round(taiNs));
  if (seekBusy[channel]) return;
  seekBusy[channel] = true;
  while (seekPending[channel] != null) {
    const t = seekPending[channel];
    seekPending[channel] = null;
    await channelPost(channel, "position", { tai_ns: t });
  }
  seekBusy[channel] = false;
}

/** IN to OUT as a new clip (automatic name). At the protection cap (409) the operator confirms. */
async function createClip(channel: number, force = false): Promise<string | undefined> {
  const ch = chan(channel);
  if (!ch) return;
  if (!ch.has_in || !ch.has_out) return void toast(channel, `Mark ${ch.has_in ? "OUT" : "IN"} first, then C`, true);
  if (ch.out_ns <= ch.in_ns) return void toast(channel, "OUT is before IN: mark OUT again", true);
  try {
    const { id } = await api.post<{ id: string }>("/api/v1/clips", { channel, force });
    await refreshLists();
    state.selectedClip = id;
    toast(channel, `Clip ${clipsById.value[id]?.name || id} created`);
    return id;
  } catch (e) {
    if (e instanceof ApiError && e.status === 409 && !force && confirm(`${e.message}\n\nCreate the clip anyway?`)) return createClip(channel, true);
    toast(channel, (e as Error).message, true);
  }
}

/** Cues a clip on a channel (a shotbox click). A clip already cued there is not clicked again: that would play it. */
export async function cueClip(channel: number, id: string) {
  const ch = chan(channel);
  const clip = clipsById.value[id];
  if (!ch || !clip) return;
  if (ch.shot_id === id && !ch.playlist && ch.shot === "cued") return toast(channel, `${clip.name} is cued · Space plays`);
  const res = await act(channel, () => api.post<{ state: string }>(`/api/v1/shotbox/${enc(id)}`, { channel }));
  if (res) toast(channel, res.state === "playing" ? `${clip.name} plays` : `${clip.name} cued · Space plays`);
  await refreshStatus();
}

/** Cues a playlist on a channel, at its first clip or at entry `index` (0-based). */
export async function cuePlaylist(channel: number, id: string, index?: number) {
  const pl = state.playlists.find((p) => p.id === id);
  if (!pl) return;
  if (!pl.entries) return toast(channel, `${pl.name} is empty`, true);
  const body = index == null ? { channel } : { channel, index };
  const status = await act(channel, () => api.post<Status>(`/api/v1/playlists/${enc(id)}/cue`, body));
  if (status) {
    applyStatus(status);
    toast(channel, index == null ? `${pl.name} cued · Space plays` : `${pl.name} ${index + 1} cued · Space plays`);
  }
}

// ------------------------------------------------------------------ clip speed and IN/OUT

async function patchClip(channel: number, id: string, body: Record<string, unknown>, force = false): Promise<boolean> {
  try {
    await api.patch(`/api/v1/clips/${enc(id)}`, force ? { ...body, force } : body);
    await refreshLists();
    return true;
  } catch (e) {
    if (e instanceof ApiError && e.status === 409 && !force && confirm(`${e.message}\n\nChange the clip anyway?`)) return patchClip(channel, id, body, true);
    toast(channel, (e as Error).message, true);
    return false;
  }
}

export async function setClipSpeed(id: string, speed: number) {
  const clip = clipsById.value[id];
  if (clip && (await patchClip(controlIndex.value, id, { speed }))) toast(controlIndex.value, `${clip.name} at ${fmtSpeed(speed)}`);
}

/** A copy of the entry's clip with the channel's marks as IN/OUT, made from the channel (its camera, IN and OUT),
 *  named "<clip> (<playlist>)"; the entry plays the copy from then on. The entry must be on the channel. */
export async function copyForEntry(channel: number) {
  const ch = chan(channel);
  const pl = state.playlist;
  const e = ch && channelEntry(ch);
  const clip = ch && loadedClip(ch);
  if (!ch || !pl || !e || !clip) return toast(channel, "Cue the entry on this channel first", true);
  if (!ch.has_in || !ch.has_out || ch.out_ns <= ch.in_ns) return toast(channel, "Mark IN and OUT first", true);
  const base = clip.name.replace(/ \(.*\)$/, "");
  let name = `${base} (${pl.name})`;
  for (let n = 2; state.clips.some((c) => c.name === name); n++) name = `${base} (${pl.name} ${n})`;
  let id = "";
  try {
    id = (await api.post<{ id: string }>("/api/v1/clips", { channel, name, colour: clip.colour, tags: clip.tags })).id;
  } catch (err) {
    if (!(err instanceof ApiError && err.status === 409 && confirm(`${err.message}\n\nMake the copy anyway?`))) return toast(channel, (err as Error).message, true);
    id = (await act(channel, () => api.post<{ id: string }>("/api/v1/clips", { channel, name, colour: clip.colour, tags: clip.tags, force: true })))?.id || "";
    if (!id) return;
  }
  await act(channel, () => api.patch(`/api/v1/clips/${enc(id)}`, { speed: clip.speed, end: clip.end }));
  const index = ch.playlist_index;
  await editPlaylist(pl.id, (entries) => {
    if (entries[index]?.clip_id === e.clip_id) entries[index].clip_id = id;
  });
  await refreshLists();
  // The engine keeps a channel on an entry with its clip: an earlier entry with the original would take it. Cue the
  // entry again (at the copy's IN, which is the marks), unless the channel plays.
  if (!chan(channel)?.playing) await cuePlaylist(channel, pl.id, index);
  toast(channel, `${name} made · entry ${index + 1} plays it`);
}

/** The channel lets go of its clip or playlist and stays where it is: marks there no longer edit the clip. */
export async function uncue(channel: number) {
  const ch = chan(channel);
  const clip = ch && loadedClip(ch);
  await transport(channel, "uncue");
  if (clip) toast(channel, `${clip.name} uncued`);
}

export async function duplicateClip(id: string) {
  const clip = clipsById.value[id];
  const copy = await act(controlIndex.value, () => api.post<Clip>(`/api/v1/clips/${enc(id)}/copy`, {}));
  if (!copy) return;
  await refreshLists();
  state.selectedClip = copy.id;
  toast(controlIndex.value, `${clip?.name || id} duplicated`);
}

/** Playlists that have the clip, and the operator's channels it is loaded on. */
export function clipUse(id: string) {
  return {
    playlists: state.playlists.filter((p) => p.clip_ids.includes(id)).map((p) => p.name),
    channels: myChannels.value.filter((c) => !c.live && loadedClip(c)?.id === id),
  };
}

export async function deleteClip(id: string) {
  const clip = clipsById.value[id];
  const ok = await act(controlIndex.value, () => api.del(`/api/v1/clips/${enc(id)}`));
  if (ok === undefined) return;
  if (state.selectedClip === id) state.selectedClip = "";
  await refreshLists();
  toast(controlIndex.value, `${clip?.name || id} deleted`);
}

// ------------------------------------------------------------------ tags

const same = (a: string, b: string) => a.toLowerCase() === b.toLowerCase();

/** A clip's tags as a list (the clip is shown with them at once, then stored). */
async function storeTags(clip: Clip, tags: string[]) {
  clip.tags = tags.join(", ");
  await patchClip(controlIndex.value, clip.id, { tags: clip.tags });
}

/** Adds a tag to a clip; a tag no clip or list has yet is added to the shared list first. */
export async function addClipTag(id: string, text: string) {
  const clip = clipsById.value[id];
  const tag = text.trim().replace(/,/g, " ").slice(0, 40).trim();
  if (!clip || !tag) return;
  const known = state.tags.find((t) => same(t.name, tag));
  if (!known) await act(controlIndex.value, () => api.post("/api/v1/tags", { name: tag }));
  const name = known?.name || tag;
  const list = tagsOf(clip);
  if (!list.some((t) => same(t, name))) await storeTags(clip, [...list, name]);
}

export async function removeClipTag(id: string, tag: string) {
  const clip = clipsById.value[id];
  if (clip) await storeTags(clip, tagsOf(clip).filter((t) => !same(t, tag)));
}

export async function renameClip(id: string, name: string) {
  const clip = clipsById.value[id];
  const text = name.trim();
  if (!clip || !text || text === clip.name) return;
  clip.name = text;
  await patchClip(controlIndex.value, id, { name: text });
}

// ------------------------------------------------------------------ entry edits

/** Reads a playlist, changes its entries and stores them: the newest entries, not the ones shown. */
export async function editPlaylist(id: string, change: (entries: PlaylistEntry[]) => void): Promise<boolean> {
  const res = await act(controlIndex.value, async () => {
    const pl = await api.get<Playlist>(`/api/v1/playlists/${enc(id)}`);
    const entries = pl.entries.map((e) => ({ ...e }));
    change(entries);
    return api.put<Playlist>(`/api/v1/playlists/${enc(id)}`, { entries });
  });
  if (!res) return false;
  if (state.playlistId === res.id) state.playlist = res;
  await refreshLists();
  return true;
}

export const setEntry = (id: string, index: number, fields: Partial<PlaylistEntry>) =>
  editPlaylist(id, (entries) => {
    if (entries[index]) Object.assign(entries[index], fields);
  });

/** Every entry of a playlist at one speed, or (`null`) each at its clip's speed: an LSM's "edit all". */
export async function setAllSpeeds(id: string, speed: number | null) {
  const ok = await editPlaylist(id, (entries) => {
    for (const e of entries) e.speed = speed ?? clipsById.value[e.clip_id]?.speed ?? e.speed;
  });
  if (ok) toast(controlIndex.value, speed == null ? "Every entry at its clip's speed" : `Every entry at ${fmtSpeed(speed)}`);
}

// ------------------------------------------------------------------ playlists

export function openPlaylist(id: string) {
  state.playlistId = id;
  store(PLAYLIST_KEY, id);
  if (state.playlist?.id !== id) state.playlist = null;
  loadPlaylist();
}

async function savePlaylist(body: Partial<Pick<Playlist, "entries" | "loop" | "name">>) {
  const pl = state.playlist;
  if (!pl) return;
  const res = await act(controlIndex.value, () => api.put<Playlist>(`/api/v1/playlists/${enc(pl.id)}`, body));
  if (res && state.playlistId === res.id) state.playlist = res;
  await refreshLists();
}

export function setEntries(entries: PlaylistEntry[]) {
  if (state.playlist) state.playlist.entries = entries; // shown at once
  return savePlaylist({ entries });
}

export function newEntry(clip: Clip): PlaylistEntry {
  return { clip_id: clip.id, speed: clip.speed, end: "next", auto_advance: true, transition: "cut", transition_frames: 0, audio_frames: -1, own_audio: true };
}

export function addToPlaylist(clipId: string, at?: number) {
  const pl = state.playlist;
  const clip = clipsById.value[clipId];
  if (!clip) return toast(controlIndex.value, "Select a clip first", true);
  if (!pl) return toast(controlIndex.value, "Open or make a playlist first", true);
  const entries = [...pl.entries];
  entries.splice(at ?? entries.length, 0, newEntry(clip));
  toast(controlIndex.value, `${clip.name} added to ${pl.name}`);
  return setEntries(entries);
}

export const toggleLoop = () => state.playlist && savePlaylist({ loop: !state.playlist.loop });

export async function newPlaylist() {
  const name = `Playlist ${state.playlists.length + 1}`;
  const res = await act(controlIndex.value, () => api.post<{ id: string }>("/api/v1/playlists", { name, entries: [] }));
  if (!res) return;
  await refreshLists();
  openPlaylist(res.id);
}
