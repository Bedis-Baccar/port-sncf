// Turns the raw trace event stream into series for the convergence charts
// and a branch-and-price tree model.
import type { TraceEvent } from "./api";

export interface BoundPoint {
  t: number;
  it: number;
  lb: number | null;
  ub: number | null;
  gap: number | null;
  pool: number | null;
  open: number | null;
  depth: number | null;
  node: number | null;
  t_master: number | null;
  t_pricing: number | null;
  t_heur: number | null;
}

export interface CgPoint {
  t: number;
  ctx: string;
  node: number;
  it: number;
  rmp: number;
  lagr: number | null;
  added: number;
  pool: number;
  min_rc: number;
}

export interface TreeNode {
  id: number;
  parent: number;
  depth: number;
  label: string;
  created: number; // solver time when it appeared
  // successive status updates (UNPROCESSED at creation, then the outcome)
  history: { t: number; status: string; lb: number | null }[];
  rule?: string;
}

export interface Incumbent {
  idx: number;
  t: number;
  value: number;
  source: string;
}

export interface TraceModel {
  start: TraceEvent | null;
  end: TraceEvent | null;
  bounds: BoundPoint[];
  cg: CgPoint[];
  incumbents: Incumbent[];
  heuristics: { t: number; duration: number; ub_before: number | null; ub_after: number | null }[];
  strategy: { t: number; value: string }[];
  nodes: Map<number, TreeNode>;
  lp: { t: number; value: number } | null;
  tMax: number;
  legacy: boolean;
}

export function emptyModel(): TraceModel {
  return {
    start: null,
    end: null,
    bounds: [],
    cg: [],
    incumbents: [],
    heuristics: [],
    strategy: [],
    nodes: new Map(),
    lp: null,
    tMax: 0,
    legacy: true,
  };
}

const num = (v: unknown): number | null => (typeof v === "number" && isFinite(v) ? v : null);

/** Mutates and returns the model (cheap incremental ingestion for live runs). */
export function ingest(m: TraceModel, events: TraceEvent[]): TraceModel {
  for (const e of events) {
    const t = num(e.t) ?? m.tMax;
    if (t > m.tMax) m.tMax = t;
    switch (e.ev) {
      case "start":
        m.start = e;
        m.legacy = false;
        break;
      case "end":
        m.end = e;
        break;
      case "bounds":
        m.bounds.push({
          t,
          it: (e.it as number) ?? m.bounds.length,
          lb: num(e.lb),
          ub: num(e.ub),
          gap: num(e.gap),
          pool: num(e.pool),
          open: num(e.open),
          depth: num(e.depth),
          node: num(e.node),
          t_master: num(e.t_master),
          t_pricing: num(e.t_pricing),
          t_heur: num(e.t_heur),
        });
        break;
      case "cg":
        m.cg.push({
          t,
          ctx: String(e.ctx),
          node: e.node as number,
          it: e.it as number,
          rmp: e.rmp as number,
          lagr: num(e.lagr),
          added: e.added as number,
          pool: e.pool as number,
          min_rc: (e.min_rc as number) ?? 0,
        });
        break;
      case "incumbent":
        m.incumbents.push({ idx: e.idx as number, t, value: e.value as number, source: String(e.source) });
        break;
      case "heuristic":
        m.heuristics.push({
          t,
          duration: (e.duration as number) ?? 0,
          ub_before: num(e.ub_before),
          ub_after: num(e.ub_after),
        });
        break;
      case "strategy":
        m.strategy.push({ t, value: String(e.value) });
        break;
      case "lp":
        m.lp = { t, value: e.value as number };
        break;
      case "node": {
        const id = e.id as number;
        let n = m.nodes.get(id);
        if (!n) {
          n = {
            id,
            parent: e.parent as number,
            depth: e.depth as number,
            label: String(e.label ?? ""),
            created: id === 0 ? 0 : t,
            history: [],
          };
          m.nodes.set(id, n);
        }
        n.history.push({ t, status: String(e.status), lb: num(e.lb) });
        break;
      }
      case "branch": {
        const children = (e.children as { id: number; label: string }[]) ?? [];
        const parent = m.nodes.get(e.parent as number);
        for (const c of children) {
          if (!m.nodes.has(c.id)) {
            m.nodes.set(c.id, {
              id: c.id,
              parent: e.parent as number,
              depth: (parent?.depth ?? 0) + 1,
              label: c.label,
              created: t,
              history: [{ t, status: "UNPROCESSED", lb: parent?.history.at(-1)?.lb ?? null }],
              rule: String(e.rule),
            });
          }
        }
        break;
      }
    }
  }
  return m;
}

export function buildModel(events: TraceEvent[]): TraceModel {
  return ingest(emptyModel(), events);
}

/** Status of a tree node at solver time t (null if not created yet). */
export function nodeStatusAt(n: TreeNode, t: number): { status: string; lb: number | null } | null {
  if (n.created > t) return null;
  let cur: { status: string; lb: number | null } = { status: "UNPROCESSED", lb: null };
  for (const h of n.history) {
    if (h.t <= t) cur = h;
    else break;
  }
  return cur;
}

export const STATUS_COLOR: Record<string, string> = {
  UNPROCESSED: "#64748b",
  PROCESSING: "#f59e0b",
  FRACTIONAL: "#3b82f6",
  INTEGER: "#22c55e",
  INFEASIBLE: "#ef4444",
  PRUNED: "#a855f7",
};

export const SOURCE_LABEL: Record<string, string> = {
  node: "integer B&P node",
  diving: "diving heuristic",
  mip: "restricted-master MIP",
};
