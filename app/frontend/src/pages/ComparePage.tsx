import type { EChartsOption } from "echarts";
import { useEffect, useMemo, useState } from "react";
import { DEFAULT_PARAMS, api, type Batch, type Health, type Method, type RunInfo, type StationSummary } from "../api";
import { fmt, fmtTime, trainColor } from "../colors";
import EChart from "../components/EChart";
import { href } from "../router";
import { buildModel, type TraceModel } from "../trace";
import { statusChip } from "./Home";

type SortKey = "id" | "objective" | "gap" | "time_s";
type View = "bp" | "lns" | "best";
const AX = { axisLine: { lineStyle: { color: "#33476e" } }, splitLine: { lineStyle: { color: "#16223b" } } };
const key = (r: { station: string | null; scenario: string | null }) => `${r.station}|${r.scenario}`;
const methodName = (m: Method) => (m === "lns" ? "LNS" : "B&P");

export default function ComparePage({ health }: { health: Health | null }) {
  const [runs, setRuns] = useState<RunInfo[]>([]);
  const [stations, setStations] = useState<StationSummary[]>([]);
  const [batches, setBatches] = useState<Batch[]>([]);
  const [picked, setPicked] = useState<string[]>([]);
  const [models, setModels] = useState<Record<string, TraceModel>>({});
  const [sort, setSort] = useState<{ k: SortKey; asc: boolean }>({ k: "id", asc: true });
  const [view, setView] = useState<View>("best");
  const [linkOnly, setLinkOnly] = useState(true);
  const [batchFilter, setBatchFilter] = useState<string>("");

  const reload = () => {
    api.runs().then(setRuns);
    api.batches().then(setBatches).catch(() => setBatches([]));
  };
  useEffect(() => {
    reload();
    api.instances().then(setStations);
  }, []);
  // refresh while a batch is running
  const batchRunning = batches.some((b) => b.jobs.some((j) => j.state === "running" || j.state === "pending"));
  useEffect(() => {
    if (!batchRunning) return;
    const t = setInterval(reload, 3000);
    return () => clearInterval(t);
  }, [batchRunning]);

  useEffect(() => {
    for (const id of picked) {
      if (!models[id]) api.events(id).then((ev) => setModels((m) => ({ ...m, [id]: buildModel(ev) })));
    }
  }, [picked]); // eslint-disable-line react-hooks/exhaustive-deps

  // runs taken into account for comparisons: same model (link conflicts) and optional batch
  const pool = useMemo(
    () => runs.filter((r) => r.objective != null && r.station && (!linkOnly || r.link_conflicts) && (!batchFilter || r.batch === batchFilter)),
    [runs, linkOnly, batchFilter],
  );

  // best lower bound known per instance (any method: B&P improves it, LNS only has the root bound)
  const bestLb = useMemo(() => {
    const m = new Map<string, number>();
    for (const r of pool) if (r.lb != null) m.set(key(r), Math.max(m.get(key(r)) ?? -Infinity, r.lb));
    return m;
  }, [pool]);

  // best run per instance and method
  const best = useMemo(() => {
    const m = new Map<string, Partial<Record<Method, RunInfo>>>();
    for (const r of pool) {
      const e = m.get(key(r)) ?? {};
      const cur = e[r.method];
      if (!cur || r.objective! < cur.objective!) e[r.method] = r;
      m.set(key(r), e);
    }
    return m;
  }, [pool]);

  const instKeys = [...best.keys()].sort();
  const gapTo = (r: RunInfo | undefined) => {
    if (!r || r.objective == null) return null;
    const lb = bestLb.get(key(r));
    return lb ? (100 * (r.objective - lb)) / lb : r.gap;
  };
  const pickView = (e: Partial<Record<Method, RunInfo>>) =>
    view === "best" ? [e.bp, e.lns].filter(Boolean).sort((a, b) => a!.objective! - b!.objective!)[0] : e[view];

  const stationNames = stations.map((s) => s.station);
  const scenarioNames = [...new Set(stations.flatMap((s) => s.scenarios.map((x) => x.scenario)))].sort();
  const heat: [number, number, number | string][] = [];
  stationNames.forEach((st, i) => scenarioNames.forEach((sc, j) => {
    const e = best.get(`${st}|${sc}`);
    const g = e ? gapTo(pickView(e)) : null;
    heat.push([j, i, g != null ? +g.toFixed(2) : "-"]);
  }));

  const heatOption: EChartsOption = {
    grid: { left: 110, right: 80, top: 10, bottom: 30 },
    tooltip: { formatter: (p: any) => `${stationNames[p.value[1]]} · ${scenarioNames[p.value[0]]}<br/>gap to best LB ${p.value[2]} %` },
    xAxis: { type: "category", data: scenarioNames, ...AX },
    yAxis: { type: "category", data: stationNames, ...AX },
    visualMap: { min: 0, max: 80, calculable: true, orient: "vertical", right: 0, top: "middle", itemHeight: 140, text: ["gap %", ""], textStyle: { color: "#cbd5e1" },
      inRange: { color: ["#16a34a", "#facc15", "#f97316", "#dc2626"] } },
    series: [{ type: "heatmap", data: heat, label: { show: true, color: "#0b1120", formatter: (p: any) => (p.value[2] === "-" ? "" : `${p.value[2]}%`) } }],
  };

  const trainsOf = (st: string, sc: string) => stations.find((s) => s.station === st)?.scenarios.find((s) => s.scenario === sc)?.trains ?? 0;
  const scatterOption: EChartsOption = {
    grid: { left: 60, right: 20, top: 30, bottom: 45 },
    legend: { top: 2, textStyle: { color: "#cbd5e1" } },
    tooltip: { formatter: (p: any) => `${p.data[3]}<br/>${p.data[0]} trains · gap ${p.data[1]} %` },
    xAxis: { type: "value", name: "trains", nameLocation: "middle", nameGap: 26, ...AX },
    yAxis: { type: "value", name: "gap to best LB %", ...AX },
    series: (["bp", "lns"] as Method[]).map((m, i) => ({
      name: methodName(m), type: "scatter" as const, color: i ? "#a855f7" : "#38bdf8", symbol: i ? "diamond" : "circle", symbolSize: 12,
      data: instKeys.flatMap((k) => {
        const r = best.get(k)?.[m];
        const g = gapTo(r);
        if (!r || g == null) return [];
        return [[trainsOf(r.station!, r.scenario!), +g.toFixed(2), r.time_s ?? 0, `${r.station} · ${r.scenario}`]];
      }),
    })),
  };

  const overlayOption: EChartsOption = {
    grid: { left: 60, right: 20, top: 34, bottom: 45 },
    legend: { top: 2, textStyle: { color: "#cbd5e1" }, type: "scroll" },
    tooltip: { trigger: "axis", valueFormatter: (v) => fmt(v as number, 4) },
    xAxis: { type: "log", name: "solver time (s, log)", nameLocation: "middle", nameGap: 26, min: 0.01, ...AX },
    yAxis: { type: "value", name: "value / best known LB", scale: true, ...AX },
    series: picked.flatMap((id, i) => {
      const m = models[id];
      const info = runs.find((r) => r.id === id);
      if (!m || !info) return [];
      const ref = bestLb.get(key(info)) ?? (m.end?.ub as number) ?? m.bounds.filter((b) => b.lb !== null).at(-1)?.lb;
      if (!ref) return [];
      const c = trainColor(i);
      const short = `${id.replace("legacy:", "")}`;
      return [
        { name: `${short} UB`, type: "line" as const, step: "end" as const, symbol: "none", color: c, data: m.bounds.filter((b) => b.ub !== null && b.t > 0).map((b) => [b.t, b.ub! / ref]) },
        { name: `${short} LB`, type: "line" as const, step: "end" as const, symbol: "none", color: c, lineStyle: { type: "dashed" as const }, data: m.bounds.filter((b) => b.lb !== null && b.t > 0).map((b) => [b.t, b.lb! / ref]) },
      ];
    }),
  };

  const sorted = useMemo(() => {
    const v = (r: RunInfo) => (sort.k === "id" ? `${r.station}${r.scenario}${r.method}${r.id}` : r[sort.k] ?? Infinity);
    return [...runs].filter((r) => !batchFilter || r.batch === batchFilter)
      .sort((a, b) => (v(a) < v(b) ? -1 : v(a) > v(b) ? 1 : 0) * (sort.asc ? 1 : -1));
  }, [runs, sort, batchFilter]);

  const th = (k: SortKey, label: string, num = false) => (
    <th className={num ? "num" : ""} style={{ cursor: "pointer" }} onClick={() => setSort((s) => ({ k, asc: s.k === k ? !s.asc : true }))}>
      {label}{sort.k === k ? (sort.asc ? " ▲" : " ▼") : ""}
    </th>
  );

  return (
    <>
      <div className="row" style={{ marginBottom: 14 }}>
        <h1>Benchmark & comparison</h1>
        <div className="right row">
          <label className="check small" title="Compare only runs of the corrected model (head-on crossings forbidden)">
            <input type="checkbox" checked={linkOnly} onChange={(e) => setLinkOnly(e.target.checked)} />corrected model only
          </label>
          <select value={batchFilter} onChange={(e) => setBatchFilter(e.target.value)}>
            <option value="">all runs</option>
            {batches.map((b) => <option key={b.id} value={b.id}>batch {b.name}</option>)}
          </select>
        </div>
      </div>

      <BatchLauncher stations={stations} health={health} batches={batches} onStarted={reload} />

      <div className="panel" style={{ marginTop: 14 }}>
        <div className="panel-head">
          <h2>Branch-and-Price vs LNS</h2>
          <span className="muted small">best run of each method per instance · gap measured to the best lower bound known for the instance</span>
        </div>
        {instKeys.length === 0 ? <div className="empty">No finished run with these filters.</div> : (
          <table className="data">
            <thead>
              <tr><th>Instance</th><th className="num">best LB</th>
                <th className="num">B&P obj</th><th className="num">gap</th><th className="num">1st sol.</th>
                <th className="num">LNS obj</th><th className="num">gap</th><th className="num">1st sol.</th><th>winner</th></tr>
            </thead>
            <tbody>
              {instKeys.map((k) => {
                const e = best.get(k)!;
                const [st, sc] = k.split("|");
                const bp = e.bp, ln = e.lns;
                const win = bp && ln ? (bp.objective! < ln.objective! ? "B&P" : ln.objective! < bp.objective! ? "LNS" : "tie") : null;
                const cell = (r?: RunInfo) => r ? (
                  <>
                    <td className="num"><a href={href.run(r.id)}>{fmt(r.objective)}</a></td>
                    <td className="num">{fmt(gapTo(r), 2)} %</td>
                    <td className="num">{fmtTime(r.ttfs ?? null)}</td>
                  </>
                ) : <><td className="num muted">—</td><td /><td /></>;
                return (
                  <tr key={k}>
                    <td style={{ textTransform: "capitalize" }}>{st} · {sc}</td>
                    <td className="num">{fmt(bestLb.get(k) ?? null, 1)}</td>
                    {cell(bp)}{cell(ln)}
                    <td>{win && <span className={`chip ${win === "tie" ? "neutral" : win === "LNS" ? "method" : "info"}`}>{win}</span>}</td>
                  </tr>
                );
              })}
            </tbody>
          </table>
        )}
      </div>

      <div className="grid cols-2" style={{ marginTop: 14 }}>
        <div className="panel">
          <div className="panel-head">
            <h2>Difficulty map</h2>
            <div className="spacer" />
            <div className="seg">
              {(["bp", "lns", "best"] as View[]).map((v) => (
                <button key={v} className={view === v ? "on" : ""} onClick={() => setView(v)}>{v === "best" ? "best of both" : methodName(v)}</button>
              ))}
            </div>
          </div>
          <EChart option={heatOption} height={280} />
        </div>
        <div className="panel">
          <div className="panel-head"><h2>Gap vs size</h2><span className="muted small">● B&P ◆ LNS</span></div>
          <EChart option={scatterOption} height={280} />
        </div>
      </div>
      <div className="panel" style={{ marginTop: 14 }}>
        <div className="panel-head"><h2>Convergence curves</h2><span className="muted small">tick runs below · normalised by the instance's best known LB (1 = optimal)</span></div>
        {picked.length ? <EChart option={overlayOption} height={320} /> : <div className="empty">Select runs to overlay their convergence.</div>}
      </div>
      <div className="panel" style={{ marginTop: 14 }}>
        <table className="data">
          <thead>
            <tr><th />{th("id", "Instance / run")}<th>Method</th><th>Status</th>{th("objective", "Objective", true)}<th className="num">LB</th>{th("gap", "Own gap", true)}{th("time_s", "Time", true)}<th>Params</th><th /></tr>
          </thead>
          <tbody>
            {sorted.map((r) => (
              <tr key={r.id}>
                <td><input type="checkbox" checked={picked.includes(r.id)} disabled={!picked.includes(r.id) && picked.length >= 6}
                  onChange={(e) => setPicked((p) => (e.target.checked ? [...p, r.id] : p.filter((x) => x !== r.id)))} /></td>
                <td><div style={{ textTransform: "capitalize" }}>{r.station} · {r.scenario}</div><div className="mono muted small">{r.id}</div></td>
                <td><span className="chip method">{methodName(r.method)}</span>{!r.link_conflicts && <span className="chip neutral" style={{ marginLeft: 4 }}>original</span>}</td>
                <td>{statusChip(r)}</td>
                <td className="num">{fmt(r.objective)}</td><td className="num">{fmt(r.lb, 1)}</td>
                <td className="num">{r.gap != null ? `${fmt(r.gap, 2)} %` : "—"}</td><td className="num">{fmtTime(r.time_s)}</td>
                <td className="small muted">{paramsSummary(r)}</td>
                <td><a href={href.run(r.id)}>open →</a></td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
    </>
  );
}

function paramsSummary(r: RunInfo): string {
  const p = r.params;
  if (!p) return r.kind === "legacy" ? "old solver" : "—";
  if (p.method === "lns") return `share ${p.lns_init_frac}→${p.lns_max_frac} · MIP ${p.lns_mip_time_s}s · ${p.lns_bound_pruning ? "bound pruning" : "no pruning"} · ops ${p.lns_operators.length}`;
  return `gaps ${p.cg_gaps.join("/")} · heur/${p.heuristic_every} · ${p.branching} · ${p.search}`;
}

/** Launch a set of runs (instances × methods) that the backend executes one after the other. */
function BatchLauncher({ stations, health, batches, onStarted }: { stations: StationSummary[]; health: Health | null; batches: Batch[]; onStarted: () => void }) {
  const [open, setOpen] = useState(false);
  const [sel, setSel] = useState<Set<string>>(new Set());
  const [methods, setMethods] = useState<Method[]>(["bp", "lns"]);
  const [time, setTime] = useState(300);
  const [link, setLink] = useState(true);
  const [name, setName] = useState("");
  const [err, setErr] = useState<string | null>(null);
  const toggle = (k: string) => setSel((s) => { const n = new Set(s); n.has(k) ? n.delete(k) : n.add(k); return n; });
  const jobs = [...sel].flatMap((k) => methods.map((m) => {
    const [station, scenario] = k.split("|");
    return { station, scenario, params: { ...DEFAULT_PARAMS, method: m, link_conflicts: link, time_limit_s: time } };
  }));
  const active = batches.filter((b) => b.jobs.some((j) => j.state === "running" || j.state === "pending"));
  const recent = batches.slice(0, 4);

  return (
    <div className="panel">
      <div className="panel-head">
        <h2>Run a benchmark</h2>
        <span className="muted small">instances × methods, executed one after the other (no CPU or license contention)</span>
        <div className="spacer" />
        {!open && <button className="primary" disabled={!health?.solver_available} onClick={() => setOpen(true)}>＋ New benchmark</button>}
      </div>
      {!health?.solver_available && <div className="muted small">The solver binary is not available: build it to launch benchmarks from here.</div>}
      {open && (
        <div className="adv" style={{ marginBottom: 12 }}>
          <div style={{ overflowX: "auto" }}>
            <table className="data" style={{ width: "auto" }}>
              <thead><tr><th />{(stations[0]?.scenarios ?? []).map((s) => <th key={s.scenario}>{s.scenario}</th>)}<th /></tr></thead>
              <tbody>
                {stations.map((st) => (
                  <tr key={st.station}>
                    <td style={{ textTransform: "capitalize" }}>{st.station}</td>
                    {st.scenarios.map((sc) => {
                      const k = `${st.station}|${sc.scenario}`;
                      return (
                        <td key={k}>
                          <label className="check small"><input type="checkbox" checked={sel.has(k)} onChange={() => toggle(k)} />{sc.trains} trains</label>
                        </td>
                      );
                    })}
                    <td><button className="ghost small" onClick={() => setSel((s) => { const n = new Set(s); const all = st.scenarios.every((x) => n.has(`${st.station}|${x.scenario}`)); st.scenarios.forEach((x) => (all ? n.delete(`${st.station}|${x.scenario}`) : n.add(`${st.station}|${x.scenario}`))); return n; })}>row</button></td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
          <div className="form" style={{ gridTemplateColumns: "repeat(4, minmax(0, 1fr))" }}>
            <div>
              <div className="muted small" style={{ marginBottom: 4 }}>Methods</div>
              {(["bp", "lns"] as Method[]).map((m) => (
                <label key={m} className="check" style={{ marginRight: 12 }}>
                  <input type="checkbox" checked={methods.includes(m)} onChange={(e) => setMethods((x) => (e.target.checked ? [...x, m] : x.filter((y) => y !== m)))} />
                  {methodName(m)}
                </label>
              ))}
            </div>
            <label className="field">Time limit per run (s)<input type="number" min={1} value={time} onChange={(e) => setTime(+e.target.value)} /></label>
            <label className="check"><input type="checkbox" checked={link} onChange={(e) => setLink(e.target.checked)} />Forbid head-on crossings</label>
            <label className="field">Name<input value={name} placeholder="optional" onChange={(e) => setName(e.target.value)} /></label>
          </div>
          {err && <div className="banner bad"><span>✕</span><div className="small">{err}</div></div>}
          <div className="row">
            <span className="muted small">{jobs.length} runs · up to {fmtTime(jobs.length * time)}</span>
            <div className="right row">
              <button className="ghost" onClick={() => setOpen(false)}>Cancel</button>
              <button className="primary" disabled={!jobs.length} onClick={async () => {
                setErr(null);
                try { await api.startBatch(name, jobs); setOpen(false); setSel(new Set()); onStarted(); } catch (e) { setErr((e as Error).message); }
              }}>▶ Launch {jobs.length} runs</button>
            </div>
          </div>
        </div>
      )}
      {recent.length > 0 && (
        <div style={{ display: "grid", gap: 8 }}>
          {recent.map((b) => {
            const done = b.jobs.filter((j) => j.state === "done").length;
            const running = b.jobs.find((j) => j.state === "running");
            return (
              <div key={b.id} className="row small" style={{ gap: 12 }}>
                <b className="mono">{b.name}</b>
                <span className="muted">{b.created.replace("T", " ")}</span>
                <div style={{ flex: 1, minWidth: 120, height: 8, background: "var(--panel-2)", borderRadius: 4, overflow: "hidden" }}>
                  <div style={{ width: `${(100 * done) / b.jobs.length}%`, height: "100%", background: "var(--accent)" }} />
                </div>
                <span>{done} / {b.jobs.length}</span>
                {running?.run_id && <a href={href.run(running.run_id, "convergence")} className="chip live info">{running.station} · {running.scenario} · {methodName(running.params.method)}</a>}
                {b.jobs.some((j) => j.state === "failed") && <span className="chip bad">{b.jobs.filter((j) => j.state === "failed").length} failed</span>}
                {active.includes(b) && <button className="danger small" onClick={async () => { await api.cancelBatch(b.id); onStarted(); }}>cancel</button>}
              </div>
            );
          })}
        </div>
      )}
    </div>
  );
}
