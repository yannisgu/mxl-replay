import { defineConfig } from "vite";
import vue from "@vitejs/plugin-vue";
import { viteSingleFile } from "vite-plugin-singlefile";

// `npm run dev` proxies the API to a running replay: REPLAY_URL (default http://localhost:8150),
// REPLAY_AUTH "user:password" for a replay behind basic auth.
const target = process.env.REPLAY_URL || "http://localhost:8150";
const auth = process.env.REPLAY_AUTH ? { Authorization: `Basic ${Buffer.from(process.env.REPLAY_AUTH).toString("base64")}` } : undefined;

export default defineConfig({
  plugins: [vue(), viteSingleFile()],
  build: {
    assetsInlineLimit: 100000000,
    cssCodeSplit: false,
  },
  server: {
    proxy: {
      "/api": { target, changeOrigin: true, ws: true, secure: false, headers: auth },
    },
  },
});
