import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

// base "./" keeps every asset URL relative so the build can be served from a
// JUCE resource provider (or any custom scheme) without a web server or
// history fallback. The app has no client-side router.
export default defineConfig({
  base: "./",
  plugins: [react()],
  build: {
    outDir: "dist",
    assetsInlineLimit: 0,
    sourcemap: true,
    target: "es2022",
    // three.js is ~570 kB minified (≈140 kB gzip) in its own lazily loaded chunk.
    chunkSizeWarningLimit: 700,
  },
});
