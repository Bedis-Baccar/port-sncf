import type { EChartsOption } from "echarts";
import { useEffect, useMemo, useState } from "react";
import { api, type RunInfo, type StationSummary } from "../api";
import { fmt, fmtTime, trainColor } from "../colors";
import EChart from "../components/EChart";
import { href } from "../router";
import { buildModel, type TraceModel } from "../trace";
import { statusChip } from "./Home";

type SortKey = "id" | "objective" | "gap" | "time_s";
const AX = { axisLine: { lineStyle: { color: "#33476e" } }, splitLine: { lineStyle: { color: "#16223b" } } };

export default function ComparePage() {
  const [runs, setRuns] = useState<RunInfo[]>([]);
  const [stations, setStations] = useState<StationSummary[]>([]);
  const [picked, setPicked] = useState<string[]>([]);
  const [models, setModels] = useState<Record<string, TraceModel>>({});
  const [sort, setSort] = useState<{ k: SortKey; asc: boolean }>({ k: "id", asc: true });

  useEffect(() => {
    api.runs().then(setRuns);
    api.instances().then(setStations);
  }, []);

  useEffect(() => {
    for (const id of picked) {
      if (!models[id]) api.events(id).then((ev) => setModels((m) => ({ ...m, [id]: buildModel(ev) })));
    }
  }, [picked]); // eslint-disable-line react-hooks/exhaustive-deps

  const sorted = useMemo(() => {
    const v = (r: RunInfo) => (sort.k === "id" ? `${r.station}${r.scenario}${r.id}` : r[sort.k] ?? Infinity);
    return [...runs].sort((a, b) => (v(a) < v(b) ? -1 : v(a) > v(b) ? 1 : 0) * (sort.asc ? 1 : -1));
  }, [runs, sort]);

  const trainsOf = (r: RunInfo) =>
    stations.find((s) => s.station === r.station)?.scenarios.find((s) => s.scenario === r.scenario)?.trains ?? null;

  // best (lowest gap, then lowest objective) finished run per instance
  const best = useMemo(() => {
    const m = new Map<string, RunInfo>();
    for (const r of runs) {
      if (!r.station || r.objective == null) continue;
      const k = `${r.station}|${r.scenario}`;
      const cur = m.get(k);
      if (!cur || (r.gap ?? 1e9) < (cur.gap ?? 1e9) || ((r.gap ?? 1e9) === (cur.gap ?? 1e9) && r.objective < (cur.objective ?? 1e18))) m.set(k, r);
    }
    return m;
  }, [runs]);

  const stationNames = stations.map((s) => s.station);
  const scenarioNames = [...new Set(stations.flatMap((s) => s.scenarios.map((x) => x.scenario)))].sort();
  const heat: [number, number, number | string][] = [];
  stationNames.forEach((st, i) => scenarioNames.forEach((sc, j) => {
    const r = best.get(`${st}|${sc}`);
    heat.push([j, i, r?.gap != null ? +r.gap.toFixed(2) : "-"]);
  }));

  const heatOption: EChartsOption = {
    grid: { left: 110, right: 80, top: 10, bottom: 30 },
    tooltip: { formatter: (p: any) => `${stationNames[p.value[1]]} · ${scenarioNames[p.value[0]]}<br/>gap ${p.value[2]} %` },
    xAxis: { type: "category", data: scenarioNames, ...AX },
    yAxis: { type: "category", data: stationNames, ...AX },
    visualMap: { min: 0, max: 80, calculable: true, orient: "vertical", right: 0, top: "middle", itemHeight: 140, text: ["gap %", ""], textStyle: { color: "#cbd5e1" },
      inRange: { color: ["#16a34a", "#facc15", "#f97316", "#dc2626"] } },
    series: [{ type: "heatmap", data: heat, label: { show: true, color: "#0b1120", formatter: (p: any) => (p.value[2] === "-" ? "" : `${p.value[2]}%`) } }],
  };

  const scatterOption: EChartsOption = {
    grid: { left: 60, right: 20, top: 30, bottom: 45 },
    legend: { top: 2, textStyle: { color: "#cbd5e1" } },
    tooltip: { formatter: (p: any) => `${p.data[3]}<br/>${p.data[0]} trains · gap ${p.data[1]} % · ${fmtTime(p.data[2])}` },
    xAxis: { type: "value", name: "trains", nameLocation: "middle", nameGap: 26, ...AX },
    yAxis: { type: "value", name: "final gap %", ...AX },
    series: stationNames.map((st, i) => ({
      name: st, type: "scatter", color: trainColor(i), symbolSize: (d: number[]) => 8 + Math.sqrt(Math.max(0, d[2])) / 3,
      data: [...best.values()].filter((r) => r.station === st && r.gap != null).map((r) => [trainsOf(r) ?? 0, +r.gap!.toFixed(2), r.time_s ?? 0, r.id]),
    })),
  };

  const overlayOption: EChartsOption = {
    grid: { left: 60, right: 20, top: 34, bottom: 45 },
    legend: { top: 2, textStyle: { color: "#cbd5e1" }, type: "scroll" },
    tooltip: { trigger: "axis", valueFormatter: (v) => fmt(v as number, 4) },
    xAxis: { type: "log", name: "solver time (s, log)", nameLocation: "middle", nameGap: 26, min: 0.01, ...AX },
    yAxis: { type: "value", name: "bound / final UB", scale: true, ...AX },
    series: picked.flatMap((id, i) => {
      const m = models[id];
      if (!m) return [];
      // normalise by the final UB (or the last LB when no incumbent was ever found)
      const ref = (m.end?.ub as number) ?? m.bounds.filter((b) => b.ub !== null).at(-1)?.ub ?? m.bounds.filter((b) => b.lb !== null).at(-1)?.lb;
      if (!ref) return [];
      const c = trainColor(i);
      const short = id.replace("legacy:", "");
      return [
        { name: `${short} LB`, type: "line" as const, step: "end" as const, symbol: "none", color: c, data: m.bounds.filter((b) => b.lb !== null && b.t > 0).map((b) => [b.t, b.lb! / ref]) },
        { name: `${short} UB`, type: "line" as const, step: "end" as const, symbol: "none", color: c, lineStyle: { type: "dashed" as const }, data: m.bounds.filter((b) => b.ub !== null && b.t > 0).map((b) => [b.t, b.ub! / ref]) },
      ];
    }),
  };

  const th = (k: SortKey, label: string, num = false) => (
    <th className={num ? "num" : ""} style={{ cursor: "pointer" }} onClick={() => setSort((s) => ({ k, asc: s.k === k ? !s.asc : true }))}>
      {label}{sort.k === k ? (sort.asc ? " ▲" : " ▼") : ""}
    </th>
  );

  return (
    <>
      <h1 style={{ marginBottom: 14 }}>Benchmark & comparison</h1>
      <div className="grid cols-2">
        <div className="panel">
          <div className="panel-head"><h2>Difficulty map</h2><span className="muted small">best final gap per instance</span></div>
          <EChart option={heatOption} height={280} />
        </div>
        <div className="panel">
          <div className="panel-head"><h2>Gap vs size</h2><span className="muted small">bubble size = solve time</span></div>
          <EChart option={scatterOption} height={280} />
        </div>
      </div>
      <div className="panel" style={{ marginTop: 14 }}>
        <div className="panel-head"><h2>Bound curves</h2><span className="muted small">tick runs in the table below (normalised by their final UB)</span></div>
        {picked.length ? <EChart option={overlayOption} height={320} /> : <div className="empty">Select runs to overlay their convergence.</div>}
      </div>
      <div className="panel" style={{ marginTop: 14 }}>
        <table className="data">
          <thead>
            <tr><th />{th("id", "Instance / run")}<th>Status</th>{th("objective", "Objective", true)}<th className="num">LB</th>{th("gap", "Gap", true)}{th("time_s", "Time", true)}<th>Params</th><th /></tr>
          </thead>
          <tbody>
            {sorted.map((r) => (
              <tr key={r.id}>
                <td><input type="checkbox" checked={picked.includes(r.id)} disabled={!picked.includes(r.id) && picked.length >= 6}
                  onChange={(e) => setPicked((p) => (e.target.checked ? [...p, r.id] : p.filter((x) => x !== r.id)))} /></td>
                <td><div style={{ textTransform: "capitalize" }}>{r.station} · {r.scenario}</div><div className="mono muted small">{r.id}</div></td>
                <td>{statusChip(r)}</td>
                <td className="num">{fmt(r.objective)}</td><td className="num">{fmt(r.lb, 1)}</td>
                <td className="num">{r.gap != null ? `${fmt(r.gap, 2)} %` : "—"}</td><td className="num">{fmtTime(r.time_s)}</td>
                <td className="small muted">{r.params ? `gaps ${r.params.cg_gaps.join("/")} · heur/${r.params.heuristic_every} · ${r.params.branching} · ${r.params.search}` : r.kind === "legacy" ? "old solver" : "—"}</td>
                <td><a href={href.run(r.id)}>open →</a></td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
    </>
  );
}
