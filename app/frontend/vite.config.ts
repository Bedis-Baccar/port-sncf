import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

// In dev, the FastAPI backend runs on :8000 and Vite proxies /api to it.
export default defineConfig({
  plugins: [react()],
  server: {
    port: 5173,
    proxy: {
      "/api": { target: "http://127.0.0.1:8000", ws: true, changeOrigin: true },
    },
  },
});
