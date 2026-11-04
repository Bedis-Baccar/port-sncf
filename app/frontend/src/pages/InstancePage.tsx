import { useEffect, useMemo, useState } from "react";
import { api, type Health, type Instance, type RunInfo } from "../api";
import { fmt, fmtTime, serviceColor } from "../colors";
import EChart from "../components/EChart";
import Gantt from "../components/Gantt";
import RunDialog from "../components/RunDialog";
import StationMap from "../components/StationMap";
import { href } from "../router";
import { statusChip } from "./Home";

export default function InstancePage({ station, scenario, health }: { station: string; scenario: string; health: Health | null }) {
  const [inst, setInst] = useState<Instance | null>(null);
  const [runs, setRuns] = useState<RunInfo[]>([]);
  const [err, setErr] = useState<string | null>(null);
  const [edit, setEdit] = useState(false);
  const [pending, setPending] = useState<Instance["layout"] | null>(null);
  const [dialog, setDialog] = useState<false | "bp" | "both">(false);

  const load = () => {
    api.instance(station, scenario).then(setInst).catch((e) => setErr(e.message));
    api.runs().then((rs) => setRuns(rs.filter((r) => r.station === station && r.scenario === scenario)));
  };
  useEffect(load, [station, scenario]);

  const loadCurve = useMemo(() => {
    if (!inst) return null;
    const xs: number[] = [];
    const present: number[] = [];
    for (let t = 0; t <= inst.T; t += inst.t_s) {
      xs.push(t);
      present.push(inst.trains.filter((tr) => tr.a_t <= t && t < tr.d_t).length);
    }
    const capacity = inst.node_types.filter((nt) => nt.dwelling).reduce((s, nt) => s + nt.nodes.length, 0);
    return { xs, present, capacity };
  }, [inst]);

  if (err) return <div className="banner bad"><span>✕</span><div>{err}</div></div>;
  if (!inst || !loadCurve) return <div className="empty"><span className="spinner" /></div>;

  const svcLoad = inst.services.map((s) => ({
    name: s.name,
    count: inst.trains.filter((t) => t.services[s.id]).length,
    nodes: inst.node_types.filter((nt) => nt.services[s.id]).reduce((a, nt) => a + nt.nodes.length, 0),
  }));

  return (
    <>
      <div className="row" style={{ marginBottom: 14 }}>
        <div>
          <div className="muted small"><a href={href.home()}>Instances</a> /</div>
          <h1 style={{ textTransform: "capitalize" }}>{station} · {scenario}</h1>
        </div>
        <div className="right row">
          <button onClick={() => setDialog("both")} title="Run Branch-and-Price and LNS with the same settings, then compare them side by side">⚖ B&P vs LNS</button>
          <button className="primary" onClick={() => setDialog("bp")}>⚙ Run solver</button>
        </div>
      </div>

      <div className="kpis" style={{ marginBottom: 14 }}>
        <div className="kpi"><div className="v">{inst.trains.length}</div><div className="l">trains</div></div>
        <div className="kpi"><div className="v">{inst.trains.reduce((a, t) => a + t.required_services.length, 0)}</div><div className="l">services required</div></div>
        <div className="kpi"><div className="v">{inst.nodes.length}</div><div className="l">physical nodes</div></div>
        <div className="kpi"><div className="v">{inst.T}</div><div className="l">horizon T (min)</div></div>
        <div className="kpi"><div className="v">{inst.t_s}</div><div className="l">time step</div></div>
        <div className="kpi"><div className="v">{fmt(inst.num_arcs)}</div><div className="l">time-space arcs</div></div>
      </div>

      <div className="panel">
        <div className="panel-head">
          <h2>Station layout</h2>
          <span className="muted small">{inst.layout_custom ? "custom layout" : "automatic layout"}</span>
          <div className="spacer" />
          {edit ? (
            <>
              <button className="primary" disabled={!pending} onClick={async () => {
                await api.saveLayout(station, pending!);
                setEdit(false); setPending(null); load();
              }}>Save layout</button>
              <button onClick={() => { setEdit(false); setPending(null); load(); }}>Cancel</button>
            </>
          ) : (
            <>
              <button onClick={() => setEdit(true)}>✎ Edit layout</button>
              {inst.layout_custom && <button className="ghost" onClick={async () => { await api.resetLayout(station); load(); }}>Reset to automatic</button>}
            </>
          )}
        </div>
        <StationMap inst={inst} editable={edit} onLayoutChange={setPending} height={440} />
      </div>

      <div className="grid cols-2" style={{ marginTop: 14 }}>
        <div className="panel">
          <div className="panel-head"><h2>Station load</h2><span className="muted small">trains present vs storage capacity</span></div>
          <EChart height={260} option={{
            grid: { left: 40, right: 16, top: 24, bottom: 30 },
            tooltip: { trigger: "axis" },
            xAxis: { type: "category", data: loadCurve.xs, name: "t" },
            yAxis: { type: "value", minInterval: 1 },
            series: [
              { type: "line", step: "end", name: "trains present", data: loadCurve.present, areaStyle: { opacity: 0.25 }, symbol: "none", color: "#38bdf8" },
              { type: "line", name: "dwelling nodes", data: loadCurve.xs.map(() => loadCurve.capacity), symbol: "none", lineStyle: { type: "dashed" }, color: "#ef4444" },
            ],
          }} />
        </div>
        <div className="panel">
          <div className="panel-head"><h2>Services</h2><span className="muted small">demand vs capable nodes</span></div>
          <table className="data">
            <thead><tr><th>Service</th><th className="num">Duration</th><th className="num">Cost</th><th className="num">Trains needing it</th><th className="num">Capable nodes</th></tr></thead>
            <tbody>
              {inst.services.map((s, i) => (
                <tr key={s.id}>
                  <td><span className="sw" style={{ display: "inline-block", width: 10, height: 10, borderRadius: 3, marginRight: 7, background: serviceColor(inst, s.name) }} />{s.name}</td>
                  <td className="num">{s.duration}</td><td className="num">{s.cost}</td>
                  <td className="num">{svcLoad[i].count}</td><td className="num">{svcLoad[i].nodes}</td>
                </tr>
              ))}
            </tbody>
          </table>
          {inst.arc_counts && (
            <EChart height={150} option={{
              grid: { left: 110, right: 30, top: 10, bottom: 10 },
              tooltip: {},
              xAxis: { type: "value", show: false },
              yAxis: { type: "category", data: Object.keys(inst.arc_counts), axisLabel: { fontSize: 10 } },
              series: [{ type: "bar", data: Object.values(inst.arc_counts), color: "#6366f1", label: { show: true, position: "right", fontSize: 10 } }],
            }} />
          )}
        </div>
      </div>

      <div className="panel" style={{ marginTop: 14 }}>
        <div className="panel-head"><h2>Demand</h2><span className="muted small">stay window [arrival, departure] and required services per train</span></div>
        <Gantt inst={inst} T={inst.T}
          rows={[...inst.trains].sort((a, b) => a.a_t - b.a_t).map((t) => ({
            id: t.id, label: `T${t.id} ${inst.train_types[t.type]?.name ?? ""}`.slice(0, 11), a_t: t.a_t, d_t: t.d_t, segments: [],
            chips: t.required_services.map((s) => inst.services[s].name),
          }))} />
      </div>

      <div className="panel" style={{ marginTop: 14 }}>
        <div className="panel-head">
          <h2>Runs on this instance</h2>
          <div className="spacer" />
          {runs.filter((r) => r.has_solution).length >= 2 && (
            <a className="btn" href={href.compare(station, scenario)}>▶ Watch runs side by side</a>
          )}
        </div>
        {runs.length === 0 ? <div className="muted">No run yet.</div> : (
          <table className="data">
            <thead><tr><th>Run</th><th>Method</th><th>Status</th><th className="num">Objective</th><th className="num">LB</th><th className="num">Gap</th><th className="num">Time</th><th /></tr></thead>
            <tbody>
              {runs.map((r) => (
                <tr key={r.id}>
                  <td className="mono">{r.id}</td>
                  <td><span className="chip method">{r.method === "lns" ? "LNS" : "B&P"}</span>{!r.link_conflicts && <span className="chip neutral" style={{ marginLeft: 4 }}>original</span>}</td>
                  <td>{statusChip(r)}</td>
                  <td className="num">{fmt(r.objective)}</td><td className="num">{fmt(r.lb, 1)}</td>
                  <td className="num">{r.gap != null ? `${fmt(r.gap, 2)} %` : "—"}</td><td className="num">{fmtTime(r.time_s)}</td>
                  <td><a href={href.run(r.id)}>open →</a></td>
                </tr>
              ))}
            </tbody>
          </table>
        )}
      </div>
      {dialog && <RunDialog station={station} scenario={scenario} solverAvailable={!!health?.solver_available}
        initialMode={dialog === "both" ? "both" : undefined} onClose={() => setDialog(false)} />}
    </>
  );
}
