import { useEffect, useState } from "react";
import { api, type Health, type RunInfo, type StationSummary } from "../api";
import { fmt, fmtTime } from "../colors";
import RunDialog from "../components/RunDialog";
import { href } from "../router";

export function statusChip(r: RunInfo | null | undefined) {
  if (!r) return <span className="chip neutral">not solved</span>;
  if (r.state === "running") return <span className="chip live info">running</span>;
  if (r.state === "failed") return <span className="chip bad">failed</span>;
  if (r.status === "OPTIMAL" || (r.gap !== null && r.gap !== undefined && r.gap < 1e-6 && r.objective !== null))
    return <span className="chip ok">optimal</span>;
  if (r.gap !== null && r.gap !== undefined) return <span className="chip warn">gap {fmt(r.gap, 1)} %</span>;
  if (r.objective !== null) return <span className="chip info">feasible</span>;
  return <span className="chip neutral">{r.state}</span>;
}

/** Most useful run to replay: running > latest v2 with a solution > legacy. */
export function bestRun(runs: RunInfo[] = []): RunInfo | null {
  const running = runs.find((r) => r.state === "running");
  if (running) return running;
  const v2 = runs.filter((r) => r.kind === "v2" && r.has_solution).sort((a, b) => (b.created ?? "").localeCompare(a.created ?? ""));
  return v2[0] ?? runs.find((r) => r.has_solution) ?? runs[0] ?? null;
}

export default function Home({ health }: { health: Health | null }) {
  const [stations, setStations] = useState<StationSummary[] | null>(null);
  const [err, setErr] = useState<string | null>(null);
  const [dialog, setDialog] = useState<{ station: string; scenario: string } | null>(null);

  useEffect(() => {
    api.instances().then(setStations).catch((e) => setErr(e.message));
  }, []);

  return (
    <>
      <div className="row" style={{ marginBottom: 18 }}>
        <div>
          <h1>Train Unit Shunting — instances</h1>
          <div className="muted">Pick a station and a scenario: explore it, replay a solution from t = 0 to the end, or run the solver.</div>
        </div>
      </div>

      {health && !health.solver_available && (
        <div className="banner warn">
          <span>⚠</span>
          <div className="small">
            <b>Replay mode.</b> The solver binary is not built yet, so only existing results can be replayed. Build it with
            CMake (needs Gurobi) — see <code>app/README.md</code> — to run instances from here.
          </div>
        </div>
      )}
      {err && <div className="banner bad"><span>✕</span><div>{err}</div></div>}
      {!stations && !err && <div className="empty"><span className="spinner" /></div>}

      {stations?.map((st) => (
        <section className="station-block" key={st.station}>
          <div className="head">
            <h2 style={{ textTransform: "capitalize" }}>{st.station}</h2>
            <span className="muted small">{st.nodes} nodes · {st.links} links</span>
          </div>
          <div className="cards">
            {st.scenarios.map((sc) => {
              const run = bestRun(sc.runs);
              return (
                <div className="card" key={sc.scenario}>
                  <div className="title">
                    <span style={{ textTransform: "capitalize" }}>{sc.scenario}</span>
                    {statusChip(run)}
                  </div>
                  {sc.error ? (
                    <div className="small" style={{ color: "#fca5a5" }}>{sc.error}</div>
                  ) : (
                    <>
                      <div className="meta">
                        <span>🚆 {sc.trains} trains</span>
                        <span>🔧 {sc.services_required} services</span>
                        <span>⏱ T={sc.T}</span>
                      </div>
                      <div className="meta">
                        {run?.objective != null ? <span>obj <b style={{ color: "var(--text)" }}>{fmt(run.objective)}</b></span> : <span>no solution yet</span>}
                        {run?.time_s != null && <span>{fmtTime(run.time_s)}</span>}
                        {(sc.runs?.length ?? 0) > 1 && <span>{sc.runs!.length} runs</span>}
                      </div>
                    </>
                  )}
                  <div className="actions">
                    <a className="btn" href={href.instance(st.station, sc.scenario)}>Explore</a>
                    {run && run.has_solution !== undefined && (run.has_solution || run.state === "running") ? (
                      <a className="btn" href={href.run(run.id, run.state === "running" ? "convergence" : "simulation")}>▶ Replay</a>
                    ) : (
                      <button disabled>▶ Replay</button>
                    )}
                    <button className="primary" disabled={!!sc.error} onClick={() => setDialog({ station: st.station, scenario: sc.scenario })}>
                      ⚙ Run
                    </button>
                  </div>
                </div>
              );
            })}
          </div>
        </section>
      ))}
      {dialog && (
        <RunDialog station={dialog.station} scenario={dialog.scenario} solverAvailable={!!health?.solver_available} onClose={() => setDialog(null)} />
      )}
    </>
  );
}
