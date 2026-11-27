// REST client and types for the replay API (SPECIFICATION.md §8.5), and display helpers.
// Times are TAI nanoseconds as JSON numbers: past 2^53 they lose their last bits, which is far below a frame.

export interface Camera {
  index: number;
  label: string;
  colour: string;
  record: boolean;
  recording: boolean;
  hfr_factor: number;
  oldest_ns: number;
  newest_ns: number;
  buffer_hours: number;
  last_frame_age_s: number;
}

export interface Channel {
  index: number;
  label: string;
  live: boolean;
  playing: boolean;
  black: boolean;
  idle: boolean;
  speed: number;
  target_speed: number;
  position_ns: number;
  camera: number;
  shot: "idle" | "cued" | "playing" | "paused" | "ended" | string;
  shot_id: string;
  clip: string;
  playlist: string;
  playlist_index: number;
  playlist_loop: boolean;
  has_in: boolean;
  has_out: boolean;
  in_ns: number;
  out_ns: number;
  in_tc: string;
  out_tc: string;
  timecode: string;
  position_tc: string;
  tc_mode: string;
  /** TSL tally: 0 off, 1 red, 2 green, 3 amber. */
  tally: number;
  clip_remaining_ns: number | null;
  playlist_remaining_ns: number | null;
  /** The owning operator (operators, §8.9); absent on a server without operators. */
  operator?: number | null;
}

export interface MultiviewCamera {
  camera: number;
  frame_ns: number | null;
  offset_ns: number | null;
  tc: string | null;
  on: number[];
}

export interface Operator {
  index: number;
  label: string;
  channels: number[];
  controlled: number[];
  primary: number;
  multiview?: {
    enabled: boolean;
    follow: number;
    position_ns: number;
    live: boolean;
    cameras: MultiviewCamera[];
  };
}

export interface Status {
  version: string;
  label: string;
  tai_ns: number;
  frame_ns: number;
  format: string;
  gpu: boolean;
  free_bytes: number;
  storage_bps: number;
  cameras: Camera[];
  channels: Channel[];
  operators?: Operator[];
  preview?: { mode: string };
}

export interface Clip {
  id: string;
  name: string;
  camera: number;
  in_ns: number;
  out_ns: number;
  in_tc: string;
  out_tc: string;
  speed: number;
  colour: string;
  tags: string;
  end: string;
  library: boolean;
  group: string;
}

/** A tag of the shared list or of a clip (GET /api/v1/tags), with the clips that have it; most used first. */
export interface TagUse {
  name: string;
  clips: number;
}

/** A playlist in the list of all playlists: the entry count and the entries' clips. */
export interface PlaylistSummary {
  id: string;
  name: string;
  loop: boolean;
  entries: number;
  clip_ids: string[];
  duration_ns: number;
}

export interface PlaylistEntry {
  clip_id: string;
  speed: number;
  end: string;
  auto_advance: boolean;
  transition: string;
  transition_frames: number;
  audio_frames?: number;
  own_audio?: boolean;
}

export interface Playlist {
  id: string;
  name: string;
  loop: boolean;
  entries: PlaylistEntry[];
  duration_ns: number;
}

export class ApiError extends Error {
  constructor(
    readonly status: number,
    readonly body: { error?: string; status?: string } | null,
  ) {
    super(body?.error || body?.status || `HTTP ${status}`);
  }
}

async function request<T>(path: string, init: RequestInit = {}): Promise<T> {
  const headers: Record<string, string> = {};
  if (typeof init.body === "string") headers["Content-Type"] = "application/json";
  const resp = await fetch(path, { ...init, headers });
  const type = resp.headers.get("content-type") || "";
  const body = type.includes("json") ? await resp.json() : await resp.text();
  if (!resp.ok) throw new ApiError(resp.status, typeof body === "object" ? body : { error: String(body).slice(0, 300) });
  return body as T;
}

export const api = {
  get: <T>(path: string) => request<T>(path),
  post: <T>(path: string, body: unknown = {}) => request<T>(path, { method: "POST", body: JSON.stringify(body) }),
  put: <T>(path: string, body: unknown = {}) => request<T>(path, { method: "PUT", body: JSON.stringify(body) }),
  patch: <T>(path: string, body: unknown = {}) => request<T>(path, { method: "PATCH", body: JSON.stringify(body) }),
  del: <T>(path: string) => request<T>(path, { method: "DELETE" }),
};

export const enc = encodeURIComponent;

/** Serial number of an id ("clip-12" → 12): creation order. */
export function serial(id: string): number {
  const m = /-(\d+)$/.exec(id || "");
  return m ? Number(m[1]) : 0;
}

/** A length: "4.2 s" under a minute, else "1:05". */
export function fmtDur(ns: number | null | undefined): string {
  if (ns == null || !Number.isFinite(ns)) return "–";
  const s = Math.max(0, ns / 1e9);
  if (s < 60) return `${s.toFixed(1)} s`;
  const h = Math.floor(s / 3600);
  const m = Math.floor((s % 3600) / 60);
  const sec = Math.floor(s % 60);
  return h ? `${h}:${String(m).padStart(2, "0")}:${String(sec).padStart(2, "0")}` : `${m}:${String(sec).padStart(2, "0")}`;
}

export const fmtSpeed = (speed: number) => `${Math.round(Math.abs(speed) * 100)}%`;

/** Playing time of a clip at a speed. */
export function playNs(clip: Clip, speed = clip.speed): number {
  const s = Math.abs(speed || 0);
  return (clip.out_ns - clip.in_ns) / (s > 0.001 ? s : 1);
}

export const tagsOf = (clip: Clip) =>
  clip.tags
    .split(",")
    .map((t) => t.trim())
    .filter(Boolean);

export const thumbUrl = (clip: Clip) => `/api/v1/clips/${enc(clip.id)}/thumbnail.jpg?in=${clip.in_ns}`;
/** A thumbnail that did not load shows its black box, not the browser's broken-image glyph. */
export function noThumb(e: Event) {
  const img = e.target as HTMLImageElement;
  if (!img.src.startsWith("data:")) img.src = "data:image/gif;base64,R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7";
}

export function fmtBytes(n: number): string {
  const u = ["B", "KB", "MB", "GB", "TB"];
  let i = 0;
  while (n >= 1000 && i < u.length - 1) {
    n /= 1000;
    ++i;
  }
  return `${n.toFixed(i > 2 ? 0 : 1)} ${u[i]}`;
}

export function transitionText(e: PlaylistEntry, first: boolean): string {
  if (first) return "Cut";
  return e.transition === "mix" ? `Mix ${e.transition_frames}` : "Cut";
}
