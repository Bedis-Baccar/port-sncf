import * as d3 from "d3";
import type { Instance, NodeType } from "./api";

// 20 well separated hues for trains, cycled for large scenarios.
const TRAIN_PALETTE = [
  ...d3.schemeTableau10,
  "#f97316", "#14b8a6", "#a3e635", "#e879f9", "#38bdf8",
  "#fb7185", "#facc15", "#818cf8", "#4ade80", "#c084fc",
];

export const trainColor = (id: number) => TRAIN_PALETTE[id % TRAIN_PALETTE.length];

const TYPE_PALETTE = ["#60a5fa", "#f472b6", "#34d399", "#fbbf24", "#a78bfa", "#fb923c", "#22d3ee", "#e879f9", "#a3e635", "#f87171"];
export const trainTypeColor = (typeId: number) => TYPE_PALETTE[typeId % TYPE_PALETTE.length];

const SERVICE_PALETTE = ["#ef4444", "#a855f7", "#14b8a6", "#0ea5e9", "#f59e0b", "#84cc16", "#ec4899"];
export function serviceColor(inst: Pick<Instance, "services"> | null, name?: string): string {
  if (!inst || !name) return "#22c55e";
  const i = inst.services.findIndex((s) => s.name === name);
  return SERVICE_PALETTE[(i < 0 ? 0 : i) % SERVICE_PALETTE.length];
}

/** Visual category of a node type: io, service, storage (parking+dwelling) or track. */
export function nodeCategory(nt: NodeType): "io" | "service" | "storage" | "track" {
  if (nt.io) return "io";
  if (nt.services.some(Boolean)) return "service";
  if (nt.dwelling) return "storage";
  return "track";
}

export function nodeTypeColor(_inst: Instance, nt: NodeType): string {
  const cat = nodeCategory(nt);
  if (cat === "io") return "#94a3b8";
  if (cat === "track") return "#475569";
  if (cat === "storage") return "#3b82f6";
  // service node: color of the first service it can perform (multi-service nodes get a mix marker)
  const first = nt.services.findIndex(Boolean);
  return SERVICE_PALETTE[first % SERVICE_PALETTE.length];
}

export const ACTIVITY_COLOR: Record<string, string> = {
  move: "#f59e0b",
  moving: "#f59e0b",
  dwell: "#64748b",
  storage: "#3b82f6",
  service: "#22c55e",
  rejected: "#ef4444",
};

export const fmt = (v: number | null | undefined, digits = 0) =>
  v === null || v === undefined || !isFinite(v) ? "—" : v.toLocaleString("en-US", { maximumFractionDigits: digits, minimumFractionDigits: digits });

export const fmtTime = (s: number | null | undefined) => {
  if (s === null || s === undefined || !isFinite(s)) return "—";
  if (s < 60) return `${s.toFixed(s < 10 ? 2 : 1)} s`;
  if (s < 3600) return `${Math.floor(s / 60)} min ${Math.round(s % 60)} s`;
  return `${Math.floor(s / 3600)} h ${Math.round((s % 3600) / 60)} min`;
};
