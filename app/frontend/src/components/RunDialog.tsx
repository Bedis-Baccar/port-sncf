import { useState } from "react";
import { DEFAULT_PARAMS, LNS_OPERATORS, api, type SolverParams } from "../api";
import { href, navigate } from "../router";

interface Props {
  station: string;
  scenario: string;
  initial?: SolverParams | null;
  solverAvailable: boolean;
  onClose: () => void;
  initialMode?: "bp" | "lns" | "both";
}

const BP_PRESETS: { name: string; hint: string; p: Partial<SolverParams> }[] = [
  { name: "Default", hint: "historical solver settings", p: {} },
  { name: "Fast primal", hint: "heuristics every 2 nodes, looser CG", p: { heuristic_every: 2, cg_gaps: [0.001, 0.005, 0.01, 0.02], mip_time_s: 10 } },
  { name: "Tight bound", hint: "exact CG everywhere, few heuristics", p: { heuristic_every: 20, cg_gaps: [0, 0, 0, 0.001] } },
  { name: "Pure B&P", hint: "no primal heuristics at all", p: { heuristic_every: 0, diving: false, mip_heuristic: false } },
];

const LNS_PRESETS: { name: string; hint: string; p: Partial<SolverParams> }[] = [
  { name: "Default", hint: "balanced", p: {} },
  { name: "Small & fast", hint: "small neighbourhoods, no MIP polish", p: { lns_init_frac: 0.15, lns_max_frac: 0.4, lns_mip_time_s: 0 } },
  { name: "Large", hint: "large neighbourhoods, longer MIP polish", p: { lns_init_frac: 0.4, lns_max_frac: 0.9, lns_mip_time_s: 15 } },
  { name: "No bound", hint: "ablation: never prune neighbourhoods by their bound", p: { lns_bound_pruning: false } },
];

const BP_KEYS: (keyof SolverParams)[] = ["cg_gaps", "heuristic_every", "diving", "dive_cg_gap", "mip_heuristic", "mip_time_s", "branching", "search", "threads"];
const LNS_KEYS: (keyof SolverParams)[] = ["lns_init_frac", "lns_max_frac", "lns_mip_time_s", "lns_root_mip_time_s", "lns_root_time_frac", "lns_cg_gap", "lns_bound_pruning", "lns_operators", "lns_max_iter", "seed"];
const pick = (keys: (keyof SolverParams)[]) => Object.fromEntries(keys.map((k) => [k, DEFAULT_PARAMS[k]])) as Partial<SolverParams>;

type Mode = "bp" | "lns" | "both";

/** Dialog that starts a solve (B&P, LNS, or both as a benchmark): method, model and every parameter. */
export default function RunDialog({ station, scenario, initial, solverAvailable, onClose, initialMode }: Props) {
  const [p, setP] = useState<SolverParams>({ ...DEFAULT_PARAMS, ...(initial ?? {}) });
  const [mode, setMode] = useState<Mode>(initialMode ?? initial?.method ?? "bp");
  const [advanced, setAdvanced] = useState(!!initial);
  const [busy, setBusy] = useState(false);
  const [err, setErr] = useState<string | null>(null);
  const set = <K extends keyof SolverParams>(k: K, v: SolverParams[K]) => setP((o) => ({ ...o, [k]: v }));
  const setGap = (i: number, v: number) => setP((o) => ({ ...o, cg_gaps: o.cg_gaps.map((g, j) => (j === i ? v : g)) }));
  const showBp = mode !== "lns";
  const showLns = mode !== "bp";

  const paramsFor = (method: "bp" | "lns"): SolverParams => {
    const base = advanced ? p : { ...DEFAULT_PARAMS, link_conflicts: p.link_conflicts, time_limit_s: p.time_limit_s };
    return { ...base, method };
  };

  const submit = async () => {
    setBusy(true);
    setErr(null);
    try {
      if (mode === "both") {
        await api.startBatch(`${station} · ${scenario} — B&P vs LNS`, [
          { station, scenario, params: paramsFor("bp") },
          { station, scenario, params: paramsFor("lns") },
        ]);
        onClose();
        navigate(href.compare(station, scenario));
      } else {
        const { id } = await api.start(station, scenario, paramsFor(mode));
        onClose();
        navigate(href.run(id, "convergence"));
      }
    } catch (e) {
      setErr((e as Error).message);
    } finally {
      setBusy(false);
    }
  };

  return (
    <div className="modal-back" onClick={onClose}>
      <div className="modal" onClick={(e) => e.stopPropagation()}>
        <div className="row">
          <h2>Solve the instance</h2>
          <span className="chip neutral right">{station} · {scenario}</span>
        </div>
        {!solverAvailable && (
          <div className="banner warn" style={{ marginTop: 12 }}>
            <span>⚠</span>
            <div className="small">
              The solver binary was not found. Build it with CMake (<code>build/cli_tusp_solver</code>) or set
              <code> TUSP_SOLVER</code>, then restart the backend.
            </div>
          </div>
        )}

        <div className="method-pick">
          <button className={mode === "bp" ? "on" : ""} onClick={() => setMode("bp")}>
            <b>Branch-and-Price</b>
            <span>exact: proves optimality (or a gap) by exploring a search tree</span>
          </button>
          <button className={mode === "lns" ? "on" : ""} onClick={() => setMode("lns")}>
            <b>Large Neighbourhood Search</b>
            <span>heuristic: repeatedly frees a few trains and re-optimises them with the pricing</span>
          </button>
          <button className={mode === "both" ? "on" : ""} onClick={() => setMode("both")}>
            <b>Both · benchmark</b>
            <span>B&P then LNS, same time limit and model, then compare them side by side on the station</span>
          </button>
        </div>

        <div className="form">
          <label className="field">
            Time limit (seconds){mode === "both" ? " per method" : ""}
            <input type="number" min={1} step={10} value={p.time_limit_s} onChange={(e) => set("time_limit_s", +e.target.value)} />
          </label>
          <label className="check" title="One capacity-1 resource per physical link and time step, used by moves in both directions">
            <input type="checkbox" checked={p.link_conflicts} onChange={(e) => set("link_conflicts", e.target.checked)} />
            <span>Forbid head-on crossings on links</span>
          </label>
          {!p.link_conflicts && (
            <div className="hint full">Original model: two trains may cross the same link in opposite directions at the same time.</div>
          )}
          <label className="check full">
            <input type="checkbox" checked={advanced} onChange={(e) => setAdvanced(e.target.checked)} />
            <span>Tune {mode === "lns" ? "LNS" : mode === "bp" ? "convergence" : "B&P and LNS"} parameters</span>
          </label>
        </div>

        {advanced && showBp && (
          <div className="adv" style={{ marginBottom: 12 }}>
            {mode === "both" && <h2 style={{ marginBottom: 8 }}>Branch-and-Price</h2>}
            <div className="row" style={{ marginBottom: 10 }}>
              <span className="muted small">Presets:</span>
              {BP_PRESETS.map((pr) => (
                <button key={pr.name} title={pr.hint} onClick={() => setP((o) => ({ ...o, ...pick(BP_KEYS), ...pr.p }))}>
                  {pr.name}
                </button>
              ))}
            </div>
            <h3 style={{ marginTop: 4 }}>Column generation</h3>
            <p className="hint">
              CG at a node stops when |RMP − Lagrangian bound| / bound ≤ gap. Smaller = stronger bounds but more pricing iterations
              (tailing-off). Different tolerance per depth of the tree.
            </p>
            <div className="form" style={{ gridTemplateColumns: "repeat(4, 1fr)", margin: "8px 0 14px" }}>
              {["root", "depth ≤ 5", "depth ≤ 10", "deeper"].map((lbl, i) => (
                <label key={lbl} className="field">
                  {lbl}
                  <input type="number" min={0} max={1} step={0.001} value={p.cg_gaps[i]} onChange={(e) => setGap(i, +e.target.value)} />
                </label>
              ))}
            </div>

            <h3>Primal heuristics</h3>
            <div className="form">
              <label className="field">
                Run every N nodes (0 = never)
                <input type="number" min={0} value={p.heuristic_every} onChange={(e) => set("heuristic_every", +e.target.value)} />
              </label>
              <div />
              <label className="check">
                <input type="checkbox" checked={p.diving} onChange={(e) => set("diving", e.target.checked)} />
                Diving (fix the most integral column, re-solve)
              </label>
              <label className="field">
                Dive CG gap
                <input type="number" min={0} max={1} step={0.01} value={p.dive_cg_gap} disabled={!p.diving} onChange={(e) => set("dive_cg_gap", +e.target.value)} />
              </label>
              <label className="check">
                <input type="checkbox" checked={p.mip_heuristic} onChange={(e) => set("mip_heuristic", e.target.checked)} />
                MIP on the restricted master
              </label>
              <label className="field">
                MIP time limit (s)
                <input type="number" min={1} value={p.mip_time_s} disabled={!p.mip_heuristic} onChange={(e) => set("mip_time_s", +e.target.value)} />
              </label>
            </div>

            <h3>Tree search</h3>
            <div className="form">
              <label className="field">
                Branching rule
                <select value={p.branching} onChange={(e) => set("branching", e.target.value as SolverParams["branching"])}>
                  <option value="auto">service split → quantum fallback</option>
                  <option value="quantum">quantum train only</option>
                </select>
              </label>
              <label className="field">
                Node selection
                <select value={p.search} onChange={(e) => set("search", e.target.value as SolverParams["search"])}>
                  <option value="bbf">best bound first</option>
                  <option value="dfs">depth first</option>
                </select>
              </label>
              <label className="field">
                Gurobi threads (0 = auto)
                <input type="number" min={0} value={p.threads} onChange={(e) => set("threads", +e.target.value)} />
              </label>
            </div>
          </div>
        )}

        {advanced && showLns && (
          <div className="adv">
            {mode === "both" && <h2 style={{ marginBottom: 8 }}>LNS</h2>}
            <div className="row" style={{ marginBottom: 10 }}>
              <span className="muted small">Presets:</span>
              {LNS_PRESETS.map((pr) => (
                <button key={pr.name} title={pr.hint} onClick={() => setP((o) => ({ ...o, ...pick(LNS_KEYS), ...pr.p }))}>
                  {pr.name}
                </button>
              ))}
            </div>
            <h3 style={{ marginTop: 4 }}>Neighbourhoods</h3>
            <p className="hint">
              Each iteration frees a share of the trains (the others keep their current path) and re-optimises them. The share grows
              when nothing improves, shrinks when the repair times out, and restarts small after reaching the maximum.
            </p>
            <div className="form">
              <label className="field">
                Initial share of trains freed
                <input type="number" min={0.01} max={1} step={0.05} value={p.lns_init_frac} onChange={(e) => set("lns_init_frac", +e.target.value)} />
              </label>
              <label className="field">
                Maximum share
                <input type="number" min={0.01} max={1} step={0.05} value={p.lns_max_frac} onChange={(e) => set("lns_max_frac", +e.target.value)} />
              </label>
              <div className="full">
                <div className="muted small" style={{ marginBottom: 6 }}>Destroy operators (chosen adaptively by success)</div>
                <div className="row">
                  {LNS_OPERATORS.map((o) => (
                    <label key={o.id} className="check" title={o.hint}>
                      <input type="checkbox" checked={p.lns_operators.includes(o.id)}
                        onChange={(e) => set("lns_operators", e.target.checked ? [...p.lns_operators, o.id] : p.lns_operators.filter((x) => x !== o.id))} />
                      {o.label}
                    </label>
                  ))}
                </div>
              </div>
            </div>
            <h3>Repair (pricing + relaxation)</h3>
            <p className="hint">
              Greedy re-insertion through the pricing, then price-and-dive: column generation pricing only the freed trains, fixing the
              most integral columns until the LP is integral. The neighbourhood's Lagrangian bound stops the work as soon as it proves
              that no solution better than UB − 1 exists there.
            </p>
            <div className="form">
              <label className="field">
                CG gap inside a neighbourhood
                <input type="number" min={0} max={1} step={0.001} value={p.lns_cg_gap} onChange={(e) => set("lns_cg_gap", +e.target.value)} />
              </label>
              <label className="check">
                <input type="checkbox" checked={p.lns_bound_pruning} onChange={(e) => set("lns_bound_pruning", e.target.checked)} />
                Prune neighbourhoods by their bound
              </label>
              <label className="field">
                MIP polish per neighbourhood (s, 0 = off)
                <input type="number" min={0} value={p.lns_mip_time_s} onChange={(e) => set("lns_mip_time_s", +e.target.value)} />
              </label>
              <label className="field">
                Max iterations (0 = time limit)
                <input type="number" min={0} value={p.lns_max_iter} onChange={(e) => set("lns_max_iter", +e.target.value)} />
              </label>
            </div>
            <h3>Root</h3>
            <div className="form">
              <label className="field">
                Root CG: share of the time limit
                <input type="number" min={0.05} max={1} step={0.05} value={p.lns_root_time_frac} onChange={(e) => set("lns_root_time_frac", +e.target.value)} />
              </label>
              <label className="field">
                Root CG gap
                <input type="number" min={0} max={1} step={0.001} value={p.cg_gaps[0]} onChange={(e) => setGap(0, +e.target.value)} />
              </label>
              <label className="field">
                MIP on the root columns (s)
                <input type="number" min={0} value={p.lns_root_mip_time_s} onChange={(e) => set("lns_root_mip_time_s", +e.target.value)} />
              </label>
              <label className="field">
                Random seed
                <input type="number" min={0} value={p.seed} onChange={(e) => set("seed", +e.target.value)} />
              </label>
            </div>
          </div>
        )}

        {err && <div className="banner bad" style={{ marginTop: 12 }}><span>✕</span><div className="small">{err}</div></div>}
        <div className="row" style={{ marginTop: 16, justifyContent: "flex-end" }}>
          <button className="ghost" onClick={onClose}>Cancel</button>
          {mode === "both" && <span className="muted small" style={{ marginRight: "auto" }}>2 runs, one after the other: up to {Math.round((2 * p.time_limit_s) / 60)} min</span>}
          <button className="primary" disabled={busy || !solverAvailable || (showLns && p.lns_operators.length === 0)} onClick={submit}>
            {busy ? <span className="spinner" /> : "▶"} {mode === "both" ? "Benchmark both" : `Start ${mode === "lns" ? "LNS" : "Branch-and-Price"}`}
          </button>
        </div>
      </div>
    </div>
  );
}
