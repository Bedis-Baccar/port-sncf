import { useState } from "react";
import { DEFAULT_PARAMS, api, type SolverParams } from "../api";
import { href, navigate } from "../router";

interface Props {
  station: string;
  scenario: string;
  initial?: SolverParams | null;
  solverAvailable: boolean;
  onClose: () => void;
}

const PRESETS: { name: string; hint: string; p: Partial<SolverParams> }[] = [
  { name: "Default", hint: "historical solver settings", p: {} },
  { name: "Fast primal", hint: "heuristics every 2 nodes, looser CG", p: { heuristic_every: 2, cg_gaps: [0.001, 0.005, 0.01, 0.02], mip_time_s: 10 } },
  { name: "Tight bound", hint: "exact CG everywhere, few heuristics", p: { heuristic_every: 20, cg_gaps: [0, 0, 0, 0.001] } },
  { name: "Pure B&P", hint: "no primal heuristics at all", p: { heuristic_every: 0, diving: false, mip_heuristic: false } },
];

export default function RunDialog({ station, scenario, initial, solverAvailable, onClose }: Props) {
  const [p, setP] = useState<SolverParams>({ ...DEFAULT_PARAMS, ...(initial ?? {}) });
  const [advanced, setAdvanced] = useState(!!initial);
  const [busy, setBusy] = useState(false);
  const [err, setErr] = useState<string | null>(null);
  const set = <K extends keyof SolverParams>(k: K, v: SolverParams[K]) => setP((o) => ({ ...o, [k]: v }));
  const setGap = (i: number, v: number) => setP((o) => ({ ...o, cg_gaps: o.cg_gaps.map((g, j) => (j === i ? v : g)) }));

  const submit = async () => {
    setBusy(true);
    setErr(null);
    try {
      const params = advanced ? p : { ...DEFAULT_PARAMS, time_limit_s: p.time_limit_s };
      const { id } = await api.start(station, scenario, params);
      onClose();
      navigate(href.run(id, "convergence"));
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
          <h2>Run the Branch-and-Price</h2>
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
        <div className="form">
          <label className="field full">
            Time limit (seconds)
            <input type="number" min={1} step={10} value={p.time_limit_s} onChange={(e) => set("time_limit_s", +e.target.value)} />
          </label>
          <label className="check full">
            <input type="checkbox" checked={advanced} onChange={(e) => setAdvanced(e.target.checked)} />
            <span>Tune convergence parameters</span>
          </label>
        </div>

        {advanced && (
          <div className="adv">
            <div className="row" style={{ marginBottom: 10 }}>
              <span className="muted small">Presets:</span>
              {PRESETS.map((pr) => (
                <button key={pr.name} title={pr.hint} onClick={() => setP({ ...DEFAULT_PARAMS, time_limit_s: p.time_limit_s, ...pr.p })}>
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

        {err && <div className="banner bad" style={{ marginTop: 12 }}><span>✕</span><div className="small">{err}</div></div>}
        <div className="row" style={{ marginTop: 16, justifyContent: "flex-end" }}>
          <button className="ghost" onClick={onClose}>Cancel</button>
          <button className="primary" disabled={busy || !solverAvailable} onClick={submit}>
            {busy ? <span className="spinner" /> : "▶"} Start solving
          </button>
        </div>
      </div>
    </div>
  );
}
