// Behringer X-Touch One over Web MIDI, in its Mackie Control mode (MC, the default). Every control acts on the
// channel under control, as the keys do.
//
//   fader                 speed, 0 to the fader's top (100 %, or 200 % with the 200% setting); the motor follows the
//                         speed (not play or pause, as the screen's fader)
//   jog wheel             steps: 1 frame, 5 frames or 1 second a detent (Scrub cycles them)
//   ◀ ▶ ▲ ▼               select a clip in the bin (▲ ▼ a row)      middle button   cue the selected clip
//   Play / Stop           play or pause / pause                      Record          live
//   ◀◀ / ▶▶               spool back / forward; again: the next of 3 speeds (2×, 5×, 10×); Play, Stop or the wheel end it
//   Fader bank ◀ ▶        previous / next camera                     Channel ◀ ▶     control A / B
//   Marker / Nudge        mark IN / OUT                              F1–F6           camera 1–6
//   REC (top, held)       shift: F1–F6 are cameras 7–12
//
// Shown on the device: the scribble strip (channel, speed or spool, camera and step, or what a button just did; red
// when on air), the timecode display (the channel's timecode), and the LEDs (shift, Play, Record when live, the
// camera's F key, the spool key, Marker / Nudge with IN / OUT set, Scrub when the step is not one frame).
//
// The X-Touch One is one strip of an 8-strip Mackie console, and Channel ◀ ▶ moves it along them: the device sends
// that strip's SELECT (0x18 strip 1, 0x19 strip 2, …), and from then on the fader on MIDI channel n, touch 0x68+n and
// REC 0x00+n, and takes the scribble strip text and the motor at strip n. So SELECT n is absolute: strip 1 controls A,
// strip 2 B, a strip past the last channel the last one; the page follows the device's strip for everything it sends.
//
// MIDI numbers are the Mackie Control protocol's. A button the page does not know shows its note number on the
// monitor, so a device that sends something else is quick to map. Web MIDI with SysEx (for the display) needs a
// secure context (https or localhost) and Chrome, Edge or Firefox.
import { reactive, watch } from "vue";
import {
  cameras,
  control,
  cueClip,
  faderSpeed,
  mark,
  myChannels,
  onAir,
  playPause,
  scrubFrames,
  scrubSeconds,
  setAngle,
  state,
  takeControl,
  toast,
  transport,
} from "./store";

const N = {
  shift: 0x00, // the strip's REC (0x00 + strip)
  bankLeft: 0x2e,
  bankRight: 0x2f,
  select: 0x18, // the strip's SELECT (0x18 + strip): what Channel ◀ ▶ send
  f1: 0x36, // F1–F6: 0x36–0x3b
  marker: 0x54,
  nudge: 0x55,
  rewind: 0x5b,
  forward: 0x5c,
  stop: 0x5d,
  play: 0x5e,
  record: 0x5f,
  up: 0x60,
  down: 0x61,
  left: 0x62,
  right: 0x63,
  middle: 0x64, // Zoom in Mackie Control
  scrub: 0x65,
  touch: 0x68, // 0x68 + strip
};
const JOG_CC = 0x3c;
const STEPS = [
  { label: "1f", frames: 1 },
  { label: "5f", frames: 5 },
  { label: "1s", frames: 0 },
];
const SPOOL = [2, 5, 10];
const STORAGE_KEY = "mxl-operator.xtouch";

export const xtouch = reactive({
  supported: typeof navigator !== "undefined" && "requestMIDIAccess" in navigator,
  device: "",
  error: "",
  shift: false,
  step: 0,
  /** Spooling: direction (−1, 1) and the index in SPOOL; 0 when not spooling. */
  spoolDir: 0,
  spoolSpeed: 0,
  touched: false,
});

/** The clip bin's order and columns, kept by ClipBin: the arrow keys walk them. */
export const bin = reactive({ order: [] as string[], columns: 1 });

let access: MIDIAccess | null = null;
let input: MIDIInput | null = null;
let output: MIDIOutput | null = null;
let releasedAt = 0;
let faderSent: number | null = null;
let leds: Record<number, boolean> = {};
let lcdSent = "";
let lcdColour = -1;
let tcSent = "";
let flash = "";
let flashTimer: number | undefined;
/** The device's strip (0–7), which Channel ◀ ▶ move: where the fader, touch, REC, text and motor are. */
let strip = 0;

function setStrip(k: number) {
  if (k === strip || k < 0 || k > 7) return;
  strip = k;
  // Everything is sent again at the new strip.
  leds = {};
  lcdSent = "";
  lcdColour = -1;
  faderSent = null;
  xtouch.touched = false;
}

function send(bytes: number[]) {
  try {
    output?.send(bytes);
  } catch {
    /* gone, or no SysEx permission */
  }
}

// ------------------------------------------------------------------ feedback on the device

function led(note: number, on: boolean) {
  if (leds[note] === on) return;
  leds[note] = on;
  send([0x90, note, on ? 0x7f : 0]);
}

const ascii = (text: string, n: number) =>
  Array.from(text.padEnd(n).slice(0, n), (c) => {
    const code = c.charCodeAt(0);
    return code >= 0x20 && code < 0x7f ? code : 0x3f;
  });

/** The scribble strip: two lines of 7 characters (Mackie Control SysEx 0x12; 0x72 the colour, X-Touch only). */
function lcd(top: string, bottom: string, colour: number) {
  const text = `${top.padEnd(7).slice(0, 7)}${bottom.padEnd(7).slice(0, 7)}`;
  if (text !== lcdSent) {
    lcdSent = text;
    send([0xf0, 0x00, 0x00, 0x66, 0x14, 0x12, 7 * strip, ...ascii(text.slice(0, 7), 7), 0xf7]);
    send([0xf0, 0x00, 0x00, 0x66, 0x14, 0x12, 0x38 + 7 * strip, ...ascii(text.slice(7), 7), 0xf7]);
  }
  if (colour !== lcdColour) {
    lcdColour = colour;
    const colours = [0, 0, 0, 0, 0, 0, 0, 0];
    colours[strip] = colour;
    send([0xf0, 0x00, 0x00, 0x66, 0x14, 0x72, ...colours, 0xf7]);
  }
}

/** The timecode display: 10 seven-segment digits, CC 0x49 (left) … 0x40 (right); Mackie's 6-bit characters. */
function timecode(tc: string) {
  const digits = tc.replace(/[^0-9]/g, "").padStart(8, "0").slice(-8);
  const text = ` ${digits.slice(0, 2)}${digits.slice(2, 4)}${digits.slice(4, 6)} ${digits.slice(6)}`; // [ HH][MM][SS][ FF]
  if (text === tcSent) return;
  for (let i = 0; i < 10; i++) {
    if (text[i] === tcSent[i]) continue;
    const code = text.charCodeAt(i);
    send([0xb0, 0x49 - i, code >= 0x40 ? code - 0x40 : code]);
  }
  tcSent = text;
}

/** A word on the strip's lower line for a moment: what a button just did. */
function say(text: string) {
  flash = text;
  clearTimeout(flashTimer);
  flashTimer = window.setTimeout(() => {
    flash = "";
    refresh();
  }, 1500);
  refresh();
}

const pct = (speed: number) => `${Math.round(speed * 100)}%`;

function refresh() {
  if (!output) return;
  const c = control.value;
  const letterOf = c ? String.fromCharCode(65 + Math.max(0, myChannels.value.findIndex((x) => x.index === c.index))) : "-";
  const playing = !!c?.playing && !c.live;
  const what = !c ? "" : c.live ? "LIVE" : xtouch.spoolDir ? `${xtouch.spoolDir < 0 ? "<<" : ">>"}${SPOOL[xtouch.spoolSpeed]}x` : playing ? pct(c.target_speed) : c.shot === "cued" ? "CUED" : "STILL";
  const bottom = xtouch.shift ? "SHIFT" : flash || `C${c?.camera ?? "-"} ${STEPS[xtouch.step].label}`;
  lcd(`${letterOf} ${what}`, bottom, c && onAir(c) ? 1 : 7);
  timecode(c?.timecode || "");

  led(N.shift + strip, xtouch.shift);
  led(N.play, playing);
  led(N.record, !!c?.live);
  led(N.scrub, xtouch.step > 0);
  led(N.marker, !!c?.has_in);
  led(N.nudge, !!c?.has_out);
  // No SELECT LEDs: the X-Touch One keeps its own lit.
  led(N.rewind, xtouch.spoolDir < 0);
  led(N.forward, xtouch.spoolDir > 0);
  const base = xtouch.shift ? 7 : 1;
  for (let i = 0; i < 6; i++) led(N.f1 + i, c?.camera === base + i);

  // The motor fader follows the speed, not while it is held or just let go (a request may still be on its way).
  if (!c || xtouch.touched || performance.now() - releasedAt < 600) return;
  // The speed, as the screen's fader shows it: pause, stop or a clip's end leave it where it is (the Play LED and the
  // strip say the channel stands); a cue moves it to the clip's speed.
  const speed = c.live ? 1 : Math.abs(c.target_speed);
  const value = Math.round(Math.max(0, Math.min(1, speed / state.faderMax)) * 16383);
  if (faderSent !== null && Math.abs(value - faderSent) < 64) return;
  faderSent = value;
  send([0xe0 + strip, value & 0x7f, value >> 7]);
}

watch(
  () => {
    const c = control.value;
    return c && [c.index, c.live, c.playing, c.target_speed, c.camera, c.timecode, c.has_in, c.has_out, c.shot, c.tally, state.faderMax];
  },
  refresh,
  { deep: true },
);

// ------------------------------------------------------------------ spooling

let spoolTimer: number | undefined;
function stopSpool() {
  if (!xtouch.spoolDir) return;
  clearInterval(spoolTimer);
  xtouch.spoolDir = 0;
  refresh();
}
async function spool(dir: number) {
  const c = control.value;
  if (!c) return;
  if (xtouch.spoolDir === dir) xtouch.spoolSpeed = (xtouch.spoolSpeed + 1) % SPOOL.length;
  else {
    xtouch.spoolSpeed = 0;
    if (!xtouch.spoolDir && (c.playing || c.live)) await transport(c.index, "pause");
  }
  xtouch.spoolDir = dir;
  clearInterval(spoolTimer);
  // The server plays at most 2×, and never backwards faster than 1×: spooling steps the position instead,
  // 10 times a second (scrubFrames sends one request at a time and adds up the rest).
  const tick = 100;
  const fps = 1e9 / (state.status?.frame_ns || 20e6);
  spoolTimer = window.setInterval(() => {
    const ch = control.value;
    if (!ch) return stopSpool();
    scrubFrames(ch.index, Math.round((dir * SPOOL[xtouch.spoolSpeed] * fps * tick) / 1000));
  }, tick);
  refresh();
}

// ------------------------------------------------------------------ actions

function selectClip(delta: number) {
  const order = bin.order;
  if (!order.length) return;
  const at = order.indexOf(state.selectedClip);
  const next = at < 0 ? (delta > 0 ? 0 : order.length - 1) : Math.max(0, Math.min(order.length - 1, at + delta));
  state.selectedClip = order[next];
}

function camera(index: number) {
  const c = control.value;
  if (!c) return;
  if (!cameras.value.some((cam) => cam.index === index)) return say(`NO C${index}`);
  setAngle(c.index, index);
}

function stepCamera(delta: number) {
  const c = control.value;
  const list = cameras.value.map((cam) => cam.index);
  if (!c || !list.length) return;
  const at = list.indexOf(c.camera);
  camera(list[Math.max(0, Math.min(list.length - 1, (at < 0 ? 0 : at) + delta))]);
}

/** SELECT of strip k (Channel ◀ ▶): the device is on strip k now; control A, B, … (past the last: the last). */
function selectStrip(k: number) {
  setStrip(k);
  const list = myChannels.value;
  const next = list[Math.min(k, list.length - 1)];
  if (next) takeControl(next.index);
  refresh();
}

function button(note: number) {
  const c = control.value;
  if (!c) return;
  if (note >= N.f1 && note < N.f1 + 6) return camera(note - N.f1 + (xtouch.shift ? 7 : 1));
  switch (note) {
    case N.play:
      stopSpool();
      return playPause(c.index);
    case N.stop:
      stopSpool();
      return transport(c.index, "pause");
    case N.record:
      stopSpool();
      return transport(c.index, "live");
    case N.rewind:
      return spool(-1);
    case N.forward:
      return spool(1);
    case N.scrub:
      xtouch.step = (xtouch.step + 1) % STEPS.length;
      return say(`STEP ${STEPS[xtouch.step].label}`);
    case N.marker:
      say("IN");
      return mark(c.index, "in");
    case N.nudge:
      say("OUT");
      return mark(c.index, "out");
    case N.left:
      return selectClip(-1);
    case N.right:
      return selectClip(1);
    case N.up:
      return selectClip(-bin.columns);
    case N.down:
      return selectClip(bin.columns);
    case N.middle:
      if (!state.selectedClip) return say("NO CLIP");
      stopSpool();
      say("CUE");
      return cueClip(c.index, state.selectedClip);
    case N.bankLeft:
      return stepCamera(-1);
    case N.bankRight:
      return stepCamera(1);
    default:
      toast(c.index, `X-Touch: button 0x${note.toString(16).padStart(2, "0")} does nothing yet`);
  }
}

function onMessage(event: MIDIMessageEvent) {
  const data = event.data;
  if (!data || data.length < 3) return;
  const [status, a, b] = data;
  const type = status & 0xf0;
  const c = control.value;
  if (type === 0xe0) {
    setStrip(status & 0x0f);
    if (!c) return;
    const value = (b << 7) | a;
    // A little dead band at both ends: the bottom is 0, the top exactly the fader's top.
    const x = value < 200 ? 0 : value > 16183 ? 1 : value / 16383;
    stopSpool();
    faderSpeed(c.index, Math.round(x * state.faderMax * 100) / 100);
  } else if (type === 0xb0 && a === JOG_CC) {
    const ticks = (b & 0x40 ? -1 : 1) * (b & 0x3f);
    if (!ticks || !c) return;
    stopSpool();
    const step = STEPS[xtouch.step];
    if (step.frames) scrubFrames(c.index, ticks * step.frames);
    else scrubSeconds(c.index, ticks);
  } else if (type === 0x90 || type === 0x80) {
    const down = type === 0x90 && b > 0;
    if (a >= N.select && a < N.select + 8) {
      if (down) selectStrip(a - N.select);
    } else if (a >= N.touch && a < N.touch + 8) {
      setStrip(a - N.touch);
      xtouch.touched = down;
      if (!down) releasedAt = performance.now();
    } else if (a >= N.shift && a < N.shift + 8) {
      xtouch.shift = down;
      refresh();
    } else if (down) button(a);
  }
}

// ------------------------------------------------------------------ connection

function remembered() {
  try {
    return localStorage.getItem(STORAGE_KEY) || "";
  } catch {
    return "";
  }
}
function remember(name: string) {
  try {
    localStorage.setItem(STORAGE_KEY, name);
  } catch {
    /* this tab only */
  }
}

function close() {
  if (input) input.onmidimessage = null;
  input = null;
  output = null;
  xtouch.device = "";
  xtouch.touched = false;
  stopSpool();
}

function use(port: MIDIInput) {
  close();
  input = port;
  input.onmidimessage = onMessage;
  output = [...access!.outputs.values()].find((o) => o.name === port.name) || null;
  xtouch.device = port.name || port.id;
  leds = {};
  lcdSent = "";
  lcdColour = -1;
  tcSent = "";
  faderSent = null;
  remember(xtouch.device);
  refresh();
}

function pick(prompted: boolean) {
  if (!access) return;
  if (input && access.inputs.get(input.id)?.state === "connected") return;
  if (input) close(); // unplugged
  const ports = [...access.inputs.values()].filter((p) => p.state === "connected");
  const want = remembered();
  const port = ports.find((p) => want && p.name === want) || ports.find((p) => /x-?touch/i.test(p.name || ""));
  if (port) return use(port);
  if (prompted) xtouch.error = ports.length ? `No X-Touch among ${ports.map((p) => p.name).join(", ")}` : "No MIDI device: plug in the X-Touch One";
}

/** Asks for MIDI (the browser prompts once) and connects the X-Touch, or the device used last time. */
export async function connectXTouch(prompted = true) {
  if (!xtouch.supported) return void (xtouch.error = "This browser has no Web MIDI: use Chrome, Edge or Firefox");
  try {
    access ??= await navigator.requestMIDIAccess({ sysex: true });
    access.onstatechange = () => pick(false);
    xtouch.error = "";
    pick(prompted);
  } catch (e) {
    xtouch.error = (e as Error).message;
  }
  if (xtouch.error && prompted && control.value) toast(control.value.index, xtouch.error, true);
}

export function disconnectXTouch() {
  lcd("", "", 0);
  close();
  remember("");
}

/** At start: connects again without a prompt when this site was allowed MIDI before. */
export async function restoreXTouch() {
  if (!xtouch.supported || !window.isSecureContext || !remembered()) return;
  try {
    const p = await navigator.permissions.query({ name: "midi", sysex: true } as unknown as PermissionDescriptor);
    if (p.state === "granted") await connectXTouch(false);
  } catch {
    /* no permission query for MIDI: wait for the button */
  }
}
