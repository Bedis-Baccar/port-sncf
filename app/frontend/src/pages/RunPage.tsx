import { useEffect, useRef, useState } from "react";
import { api, type Health } from "../api";
import { fmt, fmtTime } from "../colors";
import PlayerBar, { usePlayback } from "../components/Playback";
import RunDialog from "../components/RunDialog";
import { href, navigate } from "../router";
import { useRunData } from "../useRunData";
import ConvergenceTab from "./ConvergenceTab";
import { statusChip } from "./Home";
import SimulationTab from "./SimulationTab";
import TreeTab from "./TreeTab";

const TABS = [
  ["simulation", "🚆 Simulation"],
  ["convergence", "📈 Convergence"],
  ["tree", "🌳 Search tree"],
  ["log", "▤ Solver log"],
] as const;

export default function RunPage({ id, tab, health }: { id: string; tab: string; health: Health | null }) {
  const d = useRunData(id);
  const { info, inst, model, version, live } = d;
  const [rerun, setRerun] = useState(false);
  const [stopping, setStopping] = useState(false);

  // Solver replay clock (shared by Convergence and Tree tabs). Follows the live run by default.
  const solve = usePlayback(Math.max(model.tMax, 0.001), 0);
  const [follow, setFollow] = useState(true);
  useEffect(() => {
    if (follow) solve.setT(model.tMax);
  }, [version, follow, model.tMax]); // eslint-disable-line react-hooks/exhaustive-deps

  const logEnd = useRef<HTMLPreElement>(null);
  useEffect(() => {
    if (tab === "log" && logEnd.current) logEnd.current.scrollTop = logEnd.current.scrollHeight;
  }, [d.log, tab]);

  if (d.error) return <div className="banner bad"><span>✕</span><div>{d.error}</div></div>;
  if (!info) return <div className="empty"><span className="spinner" /></div>;

  const running = live || info.state === "running";
  const solverTab = tab === "convergence" || tab === "tree";

  return (
    <>
      <div className="row" style={{ alignItems: "flex-start" }}>
        <div>
          <div className="muted small">
            <a href={href.home()}>Instances</a> / {info.station && <a href={href.instance(info.station, info.scenario!)} style={{ textTransform: "capitalize" }}>{info.station} · {info.scenario}</a>}
          </div>
          <div className="row" style={{ marginTop: 4 }}>
            <h1 className="mono" style={{ fontSize: 18 }}>{info.kind === "legacy" ? id.replace("legacy:", "") : id}</h1>
            {running ? <span className="chip live info">LIVE</span> : statusChip(info)}
            <span className="chip method">{info.method === "lns" ? "LNS" : "Branch-and-Price"}</span>
            {!info.link_conflicts && <span className="chip neutral" title="Original model: head-on crossings on links allowed">original model</span>}
            {info.kind === "legacy" && <span className="chip neutral" title="Produced by the solver before instrumentation">old solver output</span>}
          </div>
        </div>
        <div className="right row">
          <span className="muted small">
            obj <b style={{ color: "var(--text)" }}>{fmt(info.objective ?? model.incumbents.at(-1)?.value ?? null)}</b>
            {" · "}time {fmtTime(running ? model.tMax : info.time_s)}
          </span>
          {running && (
            <button className="danger" disabled={stopping} onClick={async () => { setStopping(true); try { await api.stop(id); } finally { setStopping(false); } }}>
              ■ Stop (keeps best solution)
            </button>
          )}
          {!running && info.kind === "v2" && (
            <a className="btn" href={`/api/runs/${encodeURIComponent(id)}/download`} title="solution.json, trace.jsonl, dynamics.csv, logs, run.json">⬇ Files (ZIP)</a>
          )}
          {!running && info.station && <button onClick={() => setRerun(true)}>↻ Run again…</button>}
          {!running && info.kind === "v2" && (
            <button className="ghost" title="Delete this run" onClick={async () => {
              if (confirm(`Delete run ${id}?`)) { await api.remove(id); navigate(href.home()); }
            }}>🗑</button>
          )}
        </div>
      </div>

      <div className="tabs">
        {TABS.filter(([k]) => !(k === "tree" && (info.method === "lns" || model.method === "lns"))).map(([k, label]) => (
          <a key={k} href={href.run(id, k)} className={tab === k ? "on" : ""}>{label}</a>
        ))}
      </div>

      {solverTab && !model.legacy && (
        <PlayerBar t={solve.t} tMax={Math.max(model.tMax, 0.001)} step={Math.max(model.tMax / 200, 0.01)} playing={solve.playing}
          speed={solve.speed} speeds={[0.1, 1, 10, 60]}
          onToggle={() => { setFollow(false); solve.toggle(); }}
          onSeek={(v) => { setFollow(false); solve.setT(v); }}
          onSpeed={solve.setSpeed} format={(v) => fmtTime(v)}
          extra={<label className="check small" title="Always show the latest state"><input type="checkbox" checked={follow} onChange={(e) => setFollow(e.target.checked)} />{running ? "follow live" : "show end"}</label>} />
      )}

      {tab === "simulation" && (inst ? (
        <SimulationTab run={info} inst={inst} solution={d.solution} solutionError={d.solutionError} model={model} />
      ) : <div className="empty"><span className="spinner" /></div>)}
      {tab === "convergence" && <ConvergenceTab model={model} version={version} tSolve={model.legacy ? Infinity : solve.t} />}
      {tab === "tree" && <TreeTab model={model} version={version} tSolve={solve.t} inst={inst} />}
      {tab === "log" && (
        <div className="panel">
          <div className="panel-head"><h2>Solver output</h2>{info.params && <code className="small muted">{JSON.stringify(info.params)}</code>}</div>
          {d.log.length ? <pre className="console" ref={logEnd}>{d.log.join("\n")}</pre> : <div className="empty">No console output recorded for this run.</div>}
        </div>
      )}
      {rerun && info.station && (
        <RunDialog station={info.station} scenario={info.scenario!} initial={info.params} solverAvailable={!!health?.solver_available} onClose={() => setRerun(false)} />
      )}
    </>
  );
}
