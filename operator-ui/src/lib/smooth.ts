// Smooth timers. The replay pushes its status about 8–10 times a second (every 100 ms, checked on output grains: 120 ms
// in a 25p house), so a position or timecode shown straight from the status moves in 3–5 frame steps. Here a channel's
// position is carried on between pushes from its speed, on every animation frame, and shown in whole frames: the
// timecode counts frame by frame. A playing clip's estimate stops at its OUT. A push that is a little behind what is
// shown holds the display instead of stepping it back; a real jump (a seek, a cue, live) is shown at once.
import { reactive } from "vue";
import type { Channel } from "./api";
import { state } from "./store";

const frameNs = () => state.status?.frame_ns || 20e6;

/** performance.now(), moved on once per house frame (checked on every animation frame; the browser pauses it while
 *  the page is hidden): what shows frames re-renders 25 or 50 times a second, not at the display's 60 or 120. */
export const clock = reactive({ now: performance.now() });
let lastFrame = -1;
const tick = () => {
  const now = performance.now();
  const frame = Math.floor(now / (frameNs() / 1e6));
  if (frame !== lastFrame) {
    lastFrame = frame;
    clock.now = now;
  }
  requestAnimationFrame(tick);
};
requestAnimationFrame(tick);

/** How far a channel's picture moves per millisecond of wall time: live 1, playing its speed, else 0. */
function rate(ch: Channel) {
  if (ch.live) return 1;
  return ch.playing ? ch.speed : 0;
}

// What was shown last per channel, so a slightly late push does not step the display back.
const shown: Record<number, { ns: number; pushNs: number }> = {};
const HOLD_FRAMES = 3;

/** The channel's position now, in whole frames from the position of the last push. */
export function smoothPos(ch: Channel): number {
  const f = frameNs();
  const elapsedMs = Math.max(0, clock.now - state.statusAt);
  const moved = rate(ch) * elapsedMs * 1e6;
  // Whole frames moved since the push (towards the motion's direction).
  const frames = moved >= 0 ? Math.floor(moved / f) : Math.ceil(moved / f);
  let ns = ch.position_ns + frames * f;
  // A clip that plays stops at its OUT (the replay ends it there, or goes on to the next entry): the estimate waits
  // there for the push that says what came next, rather than running past OUT and jumping back.
  if (!ch.live && ch.clip && ch.has_out && rate(ch) > 0 && ch.position_ns <= ch.out_ns) ns = Math.min(ns, ch.out_ns);
  const last = shown[ch.index];
  if (last && rate(ch) > 0 && ns < last.ns && last.ns - ns <= HOLD_FRAMES * f) ns = last.ns; // a late push: hold
  shown[ch.index] = { ns, pushNs: ch.position_ns };
  return ns;
}

/** Frames between the channel's last pushed position and now, as smoothPos counts them. */
function framesSincePush(ch: Channel) {
  return Math.round((smoothPos(ch) - ch.position_ns) / frameNs());
}

/** "hh:mm:ss:ff" plus n frames, at `fps` frames a second, around 24 h. Unchanged when it does not read. */
function addFrames(tc: string, n: number, fps: number): string {
  const m = /^(\d+):(\d+):(\d+)([:;.])(\d+)$/.exec(tc);
  if (!m || !n) return tc;
  const day = 24 * 3600 * fps;
  let total = ((Number(m[1]) * 60 + Number(m[2])) * 60 + Number(m[3])) * fps + Number(m[5]) + n;
  total = ((total % day) + day) % day;
  const ff = total % fps;
  const s = Math.floor(total / fps);
  const p = (v: number) => String(v).padStart(2, "0");
  return `${p(Math.floor(s / 3600))}:${p(Math.floor(s / 60) % 60)}:${p(s % 60)}${m[4]}${p(ff)}`;
}

/** The channel's timecode now: the pushed one carried on by the frames moved since. */
export function smoothTimecode(ch: Channel | null | undefined): string {
  if (!ch?.timecode) return "--:--:--:--";
  const fps = Math.round(1e9 / frameNs());
  return addFrames(ch.timecode, framesSincePush(ch), fps);
}
