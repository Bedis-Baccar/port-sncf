import { useEffect, useMemo, useRef, useState } from "react";
import { api, type Conflict, type Instance, type RunInfo, type Solution, type SolvedTrain } from "../api";
import { fmt, serviceColor, trainColor } from "../colors";
import EChart from "../components/EChart";
import Gantt from "../components/Gantt";
import PlayerBar, { usePlayback } from "../components/Playback";
import StationMap from "../components/StationMap";
import TimeSpace from "../components/TimeSpace";
import { eventLog, kpisAt, stateAt } from "../sim";
import type { TraceModel } from "../trace";
import { sourceLabel } from "../trace";

interface Props {
  run: RunInfo;
  inst: Instance;
  solution: Solution | null;
  solutionError: string | null;
  model: TraceModel;
}

type Source = { kind: "best" } | { kind: "inc"; idx: number } | { kind: "lp" };

export default function SimulationTab({ run, inst, solution, solutionError, model }: Props) {
  const [source, setSource] = useState<Source>({ kind: "best" });
  const [data, setData] = useState<{ trains: SolvedTrain[]; T: number; label: string; conflicts: Conflict[]; objective: number | null } | null>(null);
  const [loadErr, setLoadErr] = useState<string | null>(null);
  const [selected, setSelected] = useState<number | null>(null);
  const [colorBy, setColorBy] = useState<"train" | "type">("train");

  useEffect(() => {
    setLoadErr(null);
    if (source.kind === "best") {
      if (solution) setData({ trains: solution.trains, T: solution.T, label: "best solution", conflicts: solution.conflicts, objective: solution.objective });
      else setData(null);
    } else if (source.kind === "inc") {
      api.incumbent(run.id, source.idx)
        .then((s) => setData({ trains: s.trains, T: s.T, label: `incumbent #${source.idx + 1}`, conflicts: s.conflicts, objective: s.value }))
        .catch((e) => setLoadErr(e.message));
    } else {
      api.lp(run.id)
        .then((lp) => setData({
          trains: lp.paths.map((p) => ({ ...p, id: p.train_id, type: inst.train_types[inst.trains[p.train_id].type]?.name ?? "", type_id: inst.trains[p.train_id].type, a_t: inst.trains[p.train_id].a_t, d_t: inst.trains[p.train_id].d_t, required_services: inst.trains[p.train_id].required_services.map((s) => inst.services[s].name) })),
          T: lp.T, label: `root LP (${lp.fractional_trains.length} fractional trains)`, conflicts: [], objective: lp.value,
        }))
        .catch((e) => setLoadErr(e.message));
    }
  }, [source, solution, run.id, inst]);

  const T = data?.T ?? inst.T;
  const pb = usePlayback(T, 0);
  const states = useMemo(() => (data ? data.trains.map((tr) => stateAt(tr, pb.t)) : []), [data, pb.t]);
  const kpis = useMemo(() => (data ? kpisAt(states, data.trains, pb.t) : null), [states, data, pb.t]);
  const events = useMemo(() => (data && source.kind !== "lp" ? eventLog(data.trains) : []), [data, source.kind]);
  const logRef = useRef<HTMLDivElement>(null);

  // keep the current event visible in the log
  useEffect(() => {
    const el = logRef.current?.querySelector(".ev.now");
    if (el && logRef.current) {
      const box = logRef.current;
      const top = (el as HTMLElement).offsetTop - box.offsetTop;
      if (top < box.scrollTop || top > box.scrollTop + box.clientHeight - 30) box.scrollTop = top - 60;
    }
  }, [pb.t]);

  const sel = data?.trains.find((t) => t.id === selected && (t.flow ?? 1) >= 0.5) ?? data?.trains.find((t) => t.id === selected);
  const nodeUse = useMemo(() => {
    if (!data) return [];
    const busy = new Map<string, number>();
    for (const tr of data.trains) for (const s of tr.segments)
      if (s.node && s.t1 > s.t0 && s.kind !== "rejected") busy.set(s.node, (busy.get(s.node) ?? 0) + (s.t1 - s.t0) * (tr.flow ?? 1));
    return inst.nodes.map((n) => ({ name: n.name, pct: (100 * (busy.get(n.name) ?? 0)) / Math.max(1, T) }))
      .filter((n) => n.pct > 0).sort((a, b) => b.pct - a.pct).slice(0, 18);
  }, [data, inst, T]);

  const costTotals = useMemo(() => {
    const acc: Record<string, number> = {};
    for (const tr of data?.trains ?? []) for (const [k, v] of Object.entries(tr.cost_breakdown)) acc[k] = (acc[k] ?? 0) + v * (tr.flow ?? 1);
    return acc;
  }, [data]);

  const accepted = data ? new Set(data.trains.filter((t) => !t.rejected).map((t) => t.id)).size : 0;
  const nodeConflicts = data ? data.conflicts.filter((c) => c.kind !== "link").length : 0;
  const crossings = data ? data.conflicts.filter((c) => c.kind === "link") : [];
  const activeCrossings = crossings.filter((c) => pb.t >= c.t && pb.t < c.t + inst.t_s);
  const rejected = data ? data.trains.filter((t) => t.rejected).length : 0;

  return (
    <>
      <div className="row" style={{ marginBottom: 6 }}>
        <label className="field" style={{ flexDirection: "row", alignItems: "center", gap: 8 }}>
          <span>Show</span>
          <select value={source.kind === "inc" ? `inc:${source.idx}` : source.kind}
            onChange={(e) => {
              const v = e.target.value;
              setSelected(null);
              setSource(v === "best" ? { kind: "best" } : v === "lp" ? { kind: "lp" } : { kind: "inc", idx: +v.split(":")[1] });
            }}>
            <option value="best">Best solution{solution?.objective != null ? ` (${fmt(solution.objective)})` : ""}</option>
            {model.lp && <option value="lp">Root LP relaxation — fractional ({fmt(model.lp.value, 1)})</option>}
            {model.incumbents.map((inc) => (
              <option key={inc.idx} value={`inc:${inc.idx}`}>
                Incumbent #{inc.idx + 1} — {fmt(inc.value)} at {inc.t.toFixed(1)} s ({sourceLabel(inc.source)})
              </option>
            ))}
          </select>
        </label>
        <div className="seg" title="Token color">
          <button className={colorBy === "train" ? "on" : ""} onClick={() => setColorBy("train")}>color by train</button>
          <button className={colorBy === "type" ? "on" : ""} onClick={() => setColorBy("type")}>by type</button>
        </div>
        <div className="right row">
          {data && <span className="chip info">{accepted} accepted</span>}
          {rejected > 0 && <span className="chip bad">{rejected} refused</span>}
          {data && source.kind !== "lp" && (nodeConflicts === 0
            ? <span className="chip ok" title="Independent check: no physical node is used by two trains at the same time (headways included)">✓ no node conflict</span>
            : <span className="chip bad">{nodeConflicts} node conflicts!</span>)}
          {data && source.kind !== "lp" && (crossings.length === 0
            ? <span className="chip ok" title="Independent check: no two trains on the same link in the same time step">✓ no head-on crossing</span>
            : <span className="chip bad" title={`Two trains cross the same link in opposite directions (allowed by the original model): ${crossings.map((c) => `${c.node} @${c.t}`).join(", ")}`}
                style={{ cursor: "pointer" }} onClick={() => { const c = crossings.find((x) => x.t > pb.t) ?? crossings[0]; pb.setT(c.t + inst.t_s / 2); setSelected(c.trains[0]); }}>
                ⚠ {crossings.length} head-on crossing{crossings.length > 1 ? "s" : ""} (next ▸)
              </span>)}
        </div>
      </div>

      {(solutionError || loadErr) && <div className="banner bad"><span>✕</span><div className="small">{loadErr ?? solutionError}</div></div>}
      {!data && !loadErr && !solutionError && (
        <div className="banner info"><span>ℹ</span><div className="small">
          {run.state === "running" ? "No integer solution yet — the simulation appears as soon as the solver finds one (follow the Convergence tab)." : "This run has no solution to replay."}
        </div></div>
      )}

      <div className="grid sim">
        <div>
          <StationMap inst={inst} states={states} selected={selected} onSelect={setSelected} colorBy={colorBy} height={500}
            alerts={activeCrossings.map((c) => ({ from: c.from!, to: c.to! }))} />
          <PlayerBar t={pb.t} tMax={T} step={inst.t_s} playing={pb.playing} speed={pb.speed} speeds={[2, 5, 10, 30, 60]}
            onToggle={pb.toggle} onSeek={(v) => pb.setT(v)} onSpeed={pb.setSpeed} format={(v) => `t = ${String(Math.floor(v)).padStart(3, "0")}`} />
        </div>
        <div className="panel" style={{ display: "flex", flexDirection: "column", gap: 12 }}>
          {kpis && (
            <div className="kpis" style={{ gridTemplateColumns: "1fr 1fr" }}>
              <div className="kpi"><div className="v">{kpis.present}</div><div className="l">in station</div></div>
              <div className="kpi"><div className="v" style={{ color: "#f59e0b" }}>{kpis.moving}</div><div className="l">shunting</div></div>
              <div className="kpi"><div className="v" style={{ color: "#22c55e" }}>{kpis.inService}</div><div className="l">in service</div></div>
              <div className="kpi"><div className="v" style={{ color: "#3b82f6" }}>{kpis.parked}</div><div className="l">parked / waiting</div></div>
              <div className="kpi"><div className="v">{kpis.servicesDone}<span className="muted small"> / {kpis.servicesRequired}</span></div><div className="l">services done</div></div>
              <div className="kpi"><div className="v">{fmt(kpis.costSoFar)}</div><div className="l">cost accrued</div></div>
            </div>
          )}
          {sel ? (
            <div className="panel" style={{ background: "var(--panel-2)" }}>
              <div className="row"><span className="sw" style={{ width: 12, height: 12, borderRadius: 3, background: trainColor(sel.id), display: "inline-block" }} />
                <b>T{sel.id}</b> <span className="muted">({sel.name}) · {sel.type}</span>
                <button className="ghost right small" onClick={() => setSelected(null)}>✕</button></div>
              <div className="small" style={{ marginTop: 6, lineHeight: 1.6 }}>
                stay {sel.a_t} → {sel.d_t} · cost <b>{fmt(sel.cost)}</b>{sel.flow !== undefined && sel.flow < 0.999 ? ` · flow ${sel.flow.toFixed(2)}` : ""}<br />
                services: {sel.required_services.map((s) => (
                  <span key={s} className="chip neutral" style={{ marginRight: 4, borderColor: serviceColor(inst, s) }}>
                    {sel.services_done.includes(s) ? "✓" : "✗"} {s}</span>
                ))}<br />
                {Object.entries(sel.cost_breakdown).map(([k, v]) => <span key={k} className="muted">{k} {v} · </span>)}
                {sel.rejected && <span className="chip bad">refused (dummy path)</span>}
              </div>
            </div>
          ) : (
            <div className="muted small">Click a train (map, diagram or Gantt) to follow it.</div>
          )}
          <h3>Events</h3>
          <div className="log" ref={logRef} style={{ flex: 1, maxHeight: 300 }}>
            {events.length === 0 && <div className="muted small">{source.kind === "lp" ? "Fractional solution: paths drawn with opacity = flow." : "—"}</div>}
            {events.map((e, i) => {
              const now = e.t <= pb.t && (events[i + 1]?.t ?? Infinity) > pb.t;
              return (
                <div key={i} className={`ev ${e.t < pb.t && !now ? "past" : ""} ${now ? "now" : ""}`}
                  onClick={() => { pb.setT(e.t); setSelected(e.train); }} style={{ cursor: "pointer" }}>
                  <span className="t">{e.t}</span>
                  <span style={{ color: e.kind === "rejected" ? "#fca5a5" : undefined }}>
                    <span style={{ color: trainColor(e.train) }}>●</span> {e.text}
                  </span>
                </div>
              );
            })}
          </div>
        </div>
      </div>

      {data && (
        <>
          <div className="panel" style={{ marginTop: 14 }}>
            <div className="panel-head"><h2>Time-space diagram</h2><span className="muted small">x = time, y = physical node · colored bars = services · click to seek</span></div>
            <TimeSpace inst={inst} trains={data.trains} T={T} t={pb.t} onSeek={pb.setT} selected={selected} onSelect={setSelected}
              crossings={crossings} />
          </div>
          <div className="panel" style={{ marginTop: 14 }}>
            <div className="panel-head">
              <h2>Train schedule</h2>
              <div className="legend">
                <span><span className="sw" style={{ background: "#f59e0b" }} />shunting</span>
                <span><span className="sw" style={{ background: "#3b82f6" }} />storage / turning</span>
                <span><span className="sw" style={{ background: "#64748b" }} />waiting</span>
                {inst.services.map((s) => <span key={s.id}><span className="sw" style={{ background: serviceColor(inst, s.name) }} />{s.name}</span>)}
              </div>
            </div>
            <Gantt inst={inst} T={T} t={pb.t} onSeek={pb.setT} selected={selected} onSelect={setSelected}
              rows={[...data.trains].filter((t) => (t.flow ?? 1) >= 0.5 || !data.trains.some((o) => o.id === t.id && (o.flow ?? 1) > (t.flow ?? 1)))
                .sort((a, b) => a.a_t - b.a_t || a.id - b.id)
                .map((t) => ({ id: t.id, label: `T${t.id} ${t.type}`.slice(0, 11), a_t: t.a_t, d_t: t.d_t, segments: t.segments, rejected: t.rejected }))} />
          </div>
          <div className="grid cols-2" style={{ marginTop: 14 }}>
            <div className="panel">
              <div className="panel-head"><h2>Cost breakdown</h2><span className="muted small">objective {fmt(data.objective)}</span></div>
              <EChart height={260} option={{
                tooltip: { trigger: "item", formatter: "{b}: {c} ({d}%)" },
                series: [{
                  type: "pie", radius: ["45%", "72%"], itemStyle: { borderColor: "#0b1120", borderWidth: 2 },
                  label: { color: "#cbd5e1", formatter: "{b}\n{d}%" },
                  data: Object.entries(costTotals).map(([k, v]) => ({
                    name: k, value: Math.round(v),
                    itemStyle: { color: { shunting: "#f59e0b", service: "#22c55e", storage: "#3b82f6", dwelling: "#64748b", rejection: "#ef4444" }[k] },
                  })),
                }],
              }} />
            </div>
            <div className="panel">
              <div className="panel-head"><h2>Node utilisation</h2><span className="muted small">% of the horizon a train stands on the node</span></div>
              <EChart height={260} option={{
                grid: { left: 60, right: 20, top: 10, bottom: 24 },
                tooltip: { trigger: "axis", valueFormatter: (v: unknown) => `${(+(v as number)).toFixed(1)} %` },
                xAxis: { type: "value", max: 100 },
                yAxis: { type: "category", data: nodeUse.map((n) => n.name).reverse(), axisLabel: { fontSize: 10 } },
                series: [{ type: "bar", data: nodeUse.map((n) => +n.pct.toFixed(1)).reverse(), color: "#22d3ee" }],
              }} />
            </div>
          </div>
        </>
      )}
    </>
  );
}
