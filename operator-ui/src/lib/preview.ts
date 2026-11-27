// The WebRTC preview (REPLAY_PREVIEW_MODE=webrtc, SPECIFICATION.md §8.6): one WHEP session per page plays the
// mosaic of every channel and camera. Each picture is a <video> on that one stream in a box with its tile's aspect
// ratio; a CSS transform scales and moves the video so only the tile shows. The session reconnects when it drops.
import { reactive } from "vue";
import { api } from "./api";

interface Tile {
  id: string;
  x: number;
  y: number;
  w: number;
  h: number;
}
interface PreviewMap {
  width: number;
  height: number;
  whep: string;
  public?: { whep?: boolean };
  tiles: Tile[];
}

export const preview = reactive({ map: null as PreviewMap | null, stream: null as MediaStream | null, connected: false });

let started = false;

// The own MediaMTX is announced on the replay's address; the page may know the host by another name.
function pageUrl(url: string, isPublic?: boolean) {
  if (isPublic) return url;
  try {
    const parsed = new URL(url);
    parsed.hostname = location.hostname;
    return parsed.toString();
  } catch {
    return url;
  }
}

async function connect() {
  let pc: RTCPeerConnection | null = null;
  let done = false;
  const again = () => {
    if (done) return;
    done = true;
    pc?.close();
    preview.connected = false;
    setTimeout(connect, 2000);
  };
  try {
    if (!preview.map) preview.map = await api.get<PreviewMap>("/api/v1/preview/map");
    const map = preview.map;
    const peer = new RTCPeerConnection();
    pc = peer;
    peer.addTransceiver("video", { direction: "recvonly" });
    const stream = new MediaStream();
    peer.ontrack = (event) => stream.addTrack(event.track);
    await peer.setLocalDescription(await peer.createOffer());
    await new Promise<void>((resolve) => {
      if (peer.iceGatheringState === "complete") return resolve();
      const timer = setTimeout(resolve, 1000);
      peer.addEventListener("icegatheringstatechange", () => {
        if (peer.iceGatheringState === "complete") {
          clearTimeout(timer);
          resolve();
        }
      });
    });
    const response = await fetch(pageUrl(map.whep, map.public?.whep), {
      method: "POST",
      headers: { "Content-Type": "application/sdp" },
      body: peer.localDescription?.sdp,
    });
    if (!response.ok) throw new Error(`WHEP ${response.status}`);
    await peer.setRemoteDescription({ type: "answer", sdp: await response.text() });
    peer.onconnectionstatechange = () => {
      preview.connected = peer.connectionState === "connected";
      if (["failed", "disconnected", "closed"].includes(peer.connectionState)) again();
    };
    preview.stream = stream;
  } catch {
    again();
  }
}

export function startPreview() {
  if (started) return;
  started = true;
  connect();
}

/** How tile `id` ("ch1", "cam2") is cut from the mosaic; null before the map is known. */
export function tileCrop(id: string) {
  const map = preview.map;
  const tile = map?.tiles.find((t) => t.id === id);
  if (!map || !tile) return null;
  return {
    box: { "--ar": String(tile.w / tile.h) },
    video: {
      objectFit: "fill" as const,
      transformOrigin: "0 0",
      transform: `scale(${map.width / tile.w}, ${map.height / tile.h}) translate(${(-100 * tile.x) / map.width}%, ${(-100 * tile.y) / map.height}%)`,
    },
  };
}
