// Typed client for the FastAPI backend (app/backend/main.py).

export interface Health {
  root: string;
  solver_path: string | null;
  solver_available: boolean;
  gurobi_home: string | null;
  license_found: boolean;
  active_runs: string[];
}

export interface RunInfo {
  id: string;
  kind: "v2" | "legacy";
  station: string | null;
  scenario: string | null;
  created: string | null;
  params: SolverParams | null;
  state: "running" | "finished" | "failed" | "stopped" | "interrupted" | "unknown";
  status: string | null;
  objective: number | null;
  lb: number | null;
  gap: number | null;
  time_s: number | null;
  has_solution: boolean;
  has_trace: boolean;
  running?: boolean;
}

export interface ScenarioSummary {
  scenario: string;
  trains?: number;
  services_required?: number;
  T?: number;
  t_s?: number;
  runs?: RunInfo[];
  error?: string;
}

export interface StationSummary {
  station: string;
  nodes: number | null;
  links: number | null;
  scenarios: ScenarioSummary[];
}

export interface NodeType {
  id: number;
  name: string;
  io: boolean;
  parking: boolean;
  dwelling: boolean;
  services: boolean[];
  nodes: number[];
}

export interface InstanceTrain {
  id: number;
  name: string;
  type: number;
  a_t: number;
  d_t: number;
  services: boolean[];
  required_services: number[];
}

export interface Instance {
  station: string;
  scenario: string;
  services: { id: number; name: string; cost: number; duration: number }[];
  node_types: NodeType[];
  nodes: { id: number; name: string; type: number }[];
  links: { id: number; origin: number; destination: number; length: number }[];
  train_types: { id: number; name: string; w_shu: number; t_turn: number }[];
  trains: InstanceTrain[];
  T: number;
  t_s: number;
  layout: Record<string, { x: number; y: number }>;
  layout_custom: boolean;
  arc_counts?: Record<string, number>;
  num_arcs?: number;
}

export type SegmentKind = "arrive" | "depart" | "move" | "service" | "storage" | "dwell" | "rejected";

export interface Segment {
  kind: SegmentKind;
  t0: number;
  t1: number;
  node?: string;
  from?: string;
  to?: string;
  service?: string;
}

export interface SolvedTrain {
  id: number;
  name: string;
  type: string;
  type_id: number;
  a_t: number;
  d_t: number;
  required_services: string[];
  segments: Segment[];
  cost: number;
  cost_breakdown: Record<string, number>;
  services_done: string[];
  rejected: boolean;
  flow?: number; // LP paths only
}

export interface Conflict {
  node: string;
  t: number;
  trains: number[];
}

export interface Solution {
  status: string | null;
  objective: number | null;
  T: number;
  t_s: number;
  graph_variant?: number;
  stats?: Record<string, number> | null;
  accepted?: number;
  rejected?: number;
  trains: SolvedTrain[];
  conflicts: Conflict[];
}

export interface LpSolution {
  t: number;
  value: number;
  T: number;
  t_s: number;
  paths: (SolvedTrain & { train_id: number; flow: number })[];
  fractional_trains: number[];
}

export interface SolverParams {
  time_limit_s: number;
  cg_gaps: number[];
  heuristic_every: number;
  diving: boolean;
  dive_cg_gap: number;
  mip_heuristic: boolean;
  mip_time_s: number;
  branching: "auto" | "quantum";
  search: "bbf" | "dfs";
  threads: number;
}

export const DEFAULT_PARAMS: SolverParams = {
  time_limit_s: 300,
  cg_gaps: [0, 0.001, 0.005, 0.01],
  heuristic_every: 5,
  diving: true,
  dive_cg_gap: 0.05,
  mip_heuristic: true,
  mip_time_s: 15,
  branching: "auto",
  search: "bbf",
  threads: 0,
};

// Trace events (see src/solver/BranchAndPriceSolver.cpp)
export type TraceEvent = { ev: string; t?: number; [k: string]: unknown };

async function req<T>(url: string, init?: RequestInit): Promise<T> {
  const r = await fetch(url, init);
  if (!r.ok) {
    let detail = r.statusText;
    try {
      const j = await r.json();
      detail = typeof j.detail === "string" ? j.detail : JSON.stringify(j.detail);
    } catch {
      /* not json */
    }
    throw new Error(detail || `HTTP ${r.status}`);
  }
  return r.json() as Promise<T>;
}

const enc = encodeURIComponent;

export const api = {
  health: () => req<Health>("/api/health"),
  instances: () => req<StationSummary[]>("/api/instances"),
  instance: (station: string, scenario: string) =>
    req<Instance>(`/api/instances/${enc(station)}/${enc(scenario)}`),
  saveLayout: (station: string, positions: Instance["layout"]) =>
    req(`/api/instances/${enc(station)}/layout`, {
      method: "PUT",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ positions }),
    }),
  resetLayout: (station: string) => req(`/api/instances/${enc(station)}/layout`, { method: "DELETE" }),
  runs: () => req<RunInfo[]>("/api/runs"),
  run: (id: string) => req<RunInfo>(`/api/runs/${enc(id)}`),
  events: (id: string) => req<TraceEvent[]>(`/api/runs/${enc(id)}/events`),
  solution: (id: string) => req<Solution>(`/api/runs/${enc(id)}/solution`),
  incumbent: (id: string, idx: number) =>
    req<Solution & { t: number; value: number; source: string }>(`/api/runs/${enc(id)}/incumbents/${idx}`),
  lp: (id: string) => req<LpSolution>(`/api/runs/${enc(id)}/lp`),
  log: (id: string) => req<{ lines: string[] }>(`/api/runs/${enc(id)}/log`),
  start: (station: string, scenario: string, params: SolverParams) =>
    req<{ id: string }>("/api/runs", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ station, scenario, params }),
    }),
  stop: (id: string) => req(`/api/runs/${enc(id)}/stop`, { method: "POST" }),
  remove: (id: string) => req(`/api/runs/${enc(id)}`, { method: "DELETE" }),
  liveUrl: (id: string) => {
    const proto = location.protocol === "https:" ? "wss" : "ws";
    return `${proto}://${location.host}/api/runs/${enc(id)}/live`;
  },
};
