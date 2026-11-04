// Typed client for the FastAPI backend (app/backend/main.py).

export interface Health {
  root: string;
  solver_path: string | null;
  solver_available: boolean;
  gurobi_home: string | null;
  license_found: boolean;
  active_runs: string[];
}

export type Method = "bp" | "lns";
export const METHOD_LABEL: Record<Method, string> = { bp: "Branch-and-Price", lns: "LNS" };

export interface RunInfo {
  id: string;
  kind: "v2" | "legacy";
  method: Method;
  link_conflicts: boolean;
  batch: string | null;
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
  ttfs?: number | null;
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
  kind: "node" | "link";
  node: string;   // node name, or "a–b" for a link
  from?: string;  // link endpoints
  to?: string;
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

export type LnsOperator = "random" | "time" | "related" | "dual" | "rejected";
export const LNS_OPERATORS: { id: LnsOperator; label: string; hint: string }[] = [
  { id: "random", label: "random", hint: "k random trains" },
  { id: "time", label: "time window", hint: "trains present at the same time as a seed train" },
  { id: "related", label: "related", hint: "trains sharing tracks with a seed train at the same time" },
  { id: "dual", label: "dual-guided", hint: "trains whose path the LP relaxation disagrees with most (high reduced cost)" },
  { id: "rejected", label: "refused", hint: "refused trains + the trains competing with them" },
];

export interface SolverParams {
  method: Method;
  link_conflicts: boolean;
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
  lns_init_frac: number;
  lns_max_frac: number;
  lns_mip_time_s: number;
  lns_root_mip_time_s: number;
  lns_root_time_frac: number;
  lns_cg_gap: number;
  lns_bound_pruning: boolean;
  lns_operators: LnsOperator[];
  lns_max_iter: number;
  seed: number;
}

export const DEFAULT_PARAMS: SolverParams = {
  method: "bp",
  link_conflicts: true,
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
  lns_init_frac: 0.3,
  lns_max_frac: 0.7,
  lns_mip_time_s: 5,
  lns_root_mip_time_s: 10,
  lns_root_time_frac: 0.3,
  lns_cg_gap: 0.001,
  lns_bound_pruning: true,
  lns_operators: ["random", "time", "related", "dual", "rejected"],
  lns_max_iter: 0,
  seed: 1,
};

export interface BatchJob {
  station: string;
  scenario: string;
  params: SolverParams;
  state: "pending" | "running" | "done" | "failed" | "cancelled" | "interrupted";
  run_id: string | null;
  error: string | null;
}
export interface Batch {
  id: string;
  name: string;
  created: string;
  jobs: BatchJob[];
  cancelled: boolean;
}

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
  batches: () => req<Batch[]>("/api/batches"),
  startBatch: (name: string, jobs: { station: string; scenario: string; params: SolverParams }[]) =>
    req<{ id: string }>("/api/batches", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ name, jobs }),
    }),
  cancelBatch: (id: string) => req(`/api/batches/${enc(id)}/cancel`, { method: "POST" }),
  remove: (id: string) => req(`/api/runs/${enc(id)}`, { method: "DELETE" }),
  liveUrl: (id: string) => {
    const proto = location.protocol === "https:" ? "wss" : "ws";
    return `${proto}://${location.host}/api/runs/${enc(id)}/live`;
  },
};
