import type { EChartsOption } from "echarts";
import { useMemo, useState } from "react";
import { fmt, fmtTime } from "../colors";
import EChart from "../components/EChart";
import LnsPanel from "../components/LnsPanel";
import type { TraceModel } from "../trace";
import { sourceLabel } from "../trace";

interface Props {
  model: TraceModel;
  version: number;
  tSolve: number;
}

const symbolFor = (src: string) =>
  src === "node" ? "circle" : src === "diving" ? "diamond" : src === "mip" || src.endsWith("mip") ? "rect"
    : src.startsWith("greedy") || src.endsWith("+greedy") ? "triangle" : src.startsWith("lns:") ? "diamond" : "circle";
const AX = { axisLine: { lineStyle: { color: "#33476e" } }, splitLine: { lineStyle: { color: "#16223b" } } };

export default function ConvergenceTab({ model, version, tSolve }: Props) {
  const [xMode, setXMode] = useState<"time" | "nodes">("time");
  const [cgNode, setCgNode] = useState<string>("node:0");
  const cgKey = model.method === "lns" && cgNode === "node:0" ? "root:0" : cgNode;

  const b = useMemo(() => model.bounds.filter((p) => p.t <= tSolve), [model, version, tSolve]); // eslint-disable-line react-hooks/exhaustive-deps
  const last = b.at(-1);
  const ended = model.end && tSolve >= model.tMax - 1e-9;
  const xv = (p: { t: number; it: number }) => (xMode === "time" ? p.t : p.it);
  const xMax = xMode === "time" ? Math.max(model.tMax, 1e-3) : Math.max(1, model.bounds.at(-1)?.it ?? 1);

  const incs = model.incumbents.filter((i) => i.t <= tSolve);
  // x of "now": step curves are extended up to it so a single value is still visible
  const xNow = xMode === "time" ? Math.min(tSolve, model.tMax) : last?.it ?? 0;
  const extend = (pts: (number | null)[][]) => (pts.length && (pts.at(-1)![0] as number) < xNow ? [...pts, [xNow, pts.at(-1)![1]]] : pts);
  const itAt = (t: number) => {
    let it = 0;
    for (const p of model.bounds) { if (p.t <= t) it = p.it; else break; }
    return it;
  };

  const boundsOption: EChartsOption = useMemo(() => ({
    grid: { left: 70, right: 20, top: 36, bottom: 40 },
    legend: { top: 4, textStyle: { color: "#cbd5e1" } },
    tooltip: { trigger: "axis", valueFormatter: (v) => fmt(v as number, 2) },
    xAxis: { type: "value", name: xMode === "time" ? "solver time (s)" : "B&P nodes", nameLocation: "middle", nameGap: 26, min: 0, max: xMax, ...AX },
    yAxis: { type: "value", scale: true, name: "objective", ...AX },
    series: [
      { name: "lower bound (LB)", type: "line", step: "end", symbol: "none", color: "#38bdf8", lineStyle: { width: 2 },
        data: extend(b.filter((p) => p.lb !== null).map((p) => [xv(p), p.lb])) },
      { name: "best integer (UB)", type: "line", step: "end", symbol: "none", color: "#22c55e", lineStyle: { width: 2 },
        data: extend(b.filter((p) => p.ub !== null).map((p) => [xv(p), p.ub])),
        markLine: {
          symbol: "none", silent: true, label: { color: "#cbd5e1", fontSize: 10 },
          data: [
            ...model.strategy.filter((s) => s.t <= tSolve).map((s) => ({ xAxis: xMode === "time" ? s.t : itAt(s.t), name: `→ ${s.value}`, lineStyle: { color: "#a855f7", type: "dashed" as const } })),
            ...(tSolve < model.tMax ? [{ xAxis: xMode === "time" ? tSolve : itAt(tSolve), name: "replay", lineStyle: { color: "#f8fafc" } }] : []),
          ],
        },
        markArea: xMode === "time" ? {
          silent: true, itemStyle: { color: "rgba(245,158,11,0.07)" },
          data: model.heuristics.filter((h) => h.t <= tSolve).map((h) => [{ xAxis: h.t - h.duration }, { xAxis: h.t }]),
        } : undefined,
      },
      { name: "new incumbent", type: "scatter", color: "#facc15", symbolSize: 11,
        data: incs.map((i) => ({ value: [xMode === "time" ? i.t : itAt(i.t), i.value], symbol: symbolFor(i.source), source: i.source })),
        tooltip: { trigger: "item", formatter: (p: any) => `new incumbent <b>${fmt(p.value[1])}</b><br/>found by ${sourceLabel(p.data.source)}` } },
      ...(model.end?.root_lb != null ? [{
        name: "root LP bound", type: "line" as const, symbol: "none", color: "#94a3b8", lineStyle: { type: "dotted" as const },
        data: [[0, model.end.root_lb as number], [xMax, model.end.root_lb as number]],
      }] : []),
    ],
  }), [b, xMode, xMax, incs, model, tSolve]); // eslint-disable-line react-hooks/exhaustive-deps

  const gapOption: EChartsOption = useMemo(() => ({
    grid: { left: 60, right: 20, top: 20, bottom: 40 },
    tooltip: { trigger: "axis", valueFormatter: (v) => `${fmt(v as number, 3)} %` },
    xAxis: { type: "value", name: xMode === "time" ? "solver time (s)" : "B&P nodes", nameLocation: "middle", nameGap: 26, min: 0, max: xMax, ...AX },
    yAxis: { type: "log", name: "gap %", min: 0.001, ...AX },
    series: [{ type: "line", step: "end", symbol: "none", color: "#f59e0b", areaStyle: { opacity: 0.15 },
      data: extend(b.filter((p) => p.gap !== null && p.ub !== null).map((p) => [xv(p), Math.max(0.001, p.gap!)])) }],
  }), [b, xMode, xMax]); // eslint-disable-line react-hooks/exhaustive-deps

  // ---- column generation per node -----------------------------------------
  const cgGroups = useMemo(() => {
    const g = new Map<string, { ctx: string; node: number; n: number; t: number }>();
    for (const c of model.cg) {
      const k = `${c.ctx}:${c.node}`;
      const e = g.get(k);
      if (e) e.n++;
      else g.set(k, { ctx: c.ctx, node: c.node, n: 1, t: c.t });
    }
    return [...g.entries()];
  }, [model, version]); // eslint-disable-line react-hooks/exhaustive-deps

  // A node can be solved several times (dives restart from it): split runs when `it` resets.
  const cgSeries = useMemo(() => {
    const [ctx, node] = cgKey.split(":");
    const pts = model.cg.filter((c) => c.ctx === ctx && String(c.node) === node && c.t <= tSolve);
    const runs: typeof pts[] = [];
    for (const p of pts) {
      if (p.it === 0 || !runs.length) runs.push([]);
      runs.at(-1)!.push(p);
    }
    return runs.at(0) ?? [];
  }, [model, version, cgKey, tSolve]); // eslint-disable-line react-hooks/exhaustive-deps

  const cgOption: EChartsOption = useMemo(() => ({
    grid: [{ left: 70, right: 60, top: 34, height: "46%" }, { left: 70, right: 60, top: "66%", bottom: 34 }],
    legend: { top: 4, textStyle: { color: "#cbd5e1" } },
    tooltip: { trigger: "axis", axisPointer: { link: [{ xAxisIndex: "all" }] }, valueFormatter: (v) => fmt(v as number, 2) },
    xAxis: [
      { type: "category", gridIndex: 0, data: cgSeries.map((c) => c.it), axisLabel: { show: false }, ...AX },
      { type: "category", gridIndex: 1, data: cgSeries.map((c) => c.it), name: "CG iteration", nameLocation: "middle", nameGap: 24, ...AX },
    ],
    yAxis: [
      { type: "value", gridIndex: 0, scale: true, name: "objective", ...AX },
      { type: "value", gridIndex: 1, name: "columns", ...AX },
      { type: "value", gridIndex: 1, name: "min r.c.", position: "right", ...AX, splitLine: { show: false } },
    ],
    series: [
      { name: "RMP objective (primal)", type: "line", xAxisIndex: 0, yAxisIndex: 0, symbol: "none", color: "#22c55e", data: cgSeries.map((c) => c.rmp) },
      { name: "Lagrangian bound (dual)", type: "line", xAxisIndex: 0, yAxisIndex: 0, symbol: "none", color: "#38bdf8", data: cgSeries.map((c) => c.lagr) },
      { name: "columns added", type: "bar", xAxisIndex: 1, yAxisIndex: 1, color: "#6366f1", data: cgSeries.map((c) => c.added) },
      { name: "most negative reduced cost", type: "line", xAxisIndex: 1, yAxisIndex: 2, symbol: "none", color: "#f472b6", data: cgSeries.map((c) => c.min_rc) },
    ],
  }), [cgSeries]);

  const poolOption: EChartsOption = useMemo(() => ({
    grid: { left: 70, right: 60, top: 30, bottom: 40 },
    legend: { top: 2, textStyle: { color: "#cbd5e1" } },
    tooltip: { trigger: "axis" },
    xAxis: { type: "value", name: "solver time (s)", nameLocation: "middle", nameGap: 26, min: 0, max: Math.max(model.tMax, 1e-3), ...AX },
    yAxis: [{ type: "value", name: "columns", ...AX }, { type: "value", name: "open nodes", position: "right", ...AX, splitLine: { show: false } }],
    series: [
      { name: "column pool", type: "line", symbol: "none", color: "#a78bfa", areaStyle: { opacity: 0.12 }, data: b.filter((p) => p.pool !== null).map((p) => [p.t, p.pool]) },
      { name: "open nodes", type: "line", step: "end", symbol: "none", yAxisIndex: 1, color: "#f59e0b", data: b.filter((p) => p.open !== null).map((p) => [p.t, p.open]) },
    ],
  }), [b, model.tMax]);

  const prof = b.filter((p) => p.t_master !== null);
  const endProf = model.end ? { master: model.end.t_master as number, pricing: model.end.t_pricing as number, heur: model.end.t_heur as number, total: model.end.t as number } : null;
  const profOption: EChartsOption = useMemo(() => ({
    grid: { left: 60, right: 20, top: 30, bottom: 40 },
    legend: { top: 2, textStyle: { color: "#cbd5e1" } },
    tooltip: { trigger: "axis", valueFormatter: (v) => fmtTime(v as number) },
    xAxis: { type: "value", name: "solver time (s)", nameLocation: "middle", nameGap: 26, min: 0, max: Math.max(model.tMax, 1e-3), ...AX },
    yAxis: { type: "value", name: "cumulated s", ...AX },
    series: [
      // cumulative bands drawn explicitly (ECharts stacking on a value x-axis is unreliable)
      { name: "pricing", type: "line", symbol: "none", areaStyle: { opacity: 0.55 }, color: "#22c55e", data: prof.map((p) => [p.t, (p.t_master ?? 0) + (p.t_pricing ?? 0)]) },
      { name: "master LP", type: "line", symbol: "none", areaStyle: { opacity: 0.85 }, color: "#3b82f6", data: prof.map((p) => [p.t, p.t_master]) },
      { name: model.method === "lns" ? "MIP (root + polish)" : "heuristics (incl. their CG)", type: "line", symbol: "none", lineStyle: { type: "dashed" }, color: "#f59e0b", data: prof.map((p) => [p.t, p.t_heur]) },
    ],
  }), [prof, model.tMax, model.method]); // eslint-disable-line react-hooks/exhaustive-deps

  const kRootLb = (model.end?.root_lb as number | undefined) ?? model.nodes.get(0)?.history.find((h) => h.status !== "UNPROCESSED")?.lb ?? null;
  const lb = ended ? (model.end!.lb as number) : last?.lb ?? null;
  const ub = ended ? (model.end!.ub as number) : last?.ub ?? null;
  const gap = ended ? (model.end!.gap as number) : last?.gap ?? null;

  return (
    <>
      {model.legacy && (
        <div className="banner info"><span>ℹ</span><div className="small">
          This run comes from the <b>old solver</b>: only the LB/UB timeline is available. Re-run the instance with the new
          solver to get column-generation iterations, incumbents, the search tree and the root LP.
        </div></div>
      )}
      <div className="kpis" style={{ marginBottom: 14 }}>
        <div className="kpi"><div className="v" style={{ color: "#94a3b8" }}>{fmt(kRootLb, 1)}</div><div className="l">root LB</div></div>
        <div className="kpi"><div className="v" style={{ color: "#38bdf8" }}>{fmt(lb, 1)}</div><div className="l">lower bound</div></div>
        <div className="kpi"><div className="v" style={{ color: "#22c55e" }}>{fmt(ub)}</div><div className="l">best integer</div></div>
        <div className="kpi"><div className="v" style={{ color: "#f59e0b" }}>{gap != null && ub != null ? `${fmt(gap, 2)} %` : "—"}</div><div className="l">gap</div></div>
        <div className="kpi"><div className="v">{fmt(ended ? (model.end!.nodes as number) : last?.it ?? null)}</div><div className="l">{model.method === "lns" ? "LNS iterations" : "nodes explored"}</div></div>
        <div className="kpi"><div className="v">{fmt(last?.open ?? null)}</div><div className="l">open nodes</div></div>
        <div className="kpi"><div className="v">{fmt(last?.pool ?? null)}</div><div className="l">columns</div></div>
        <div className="kpi"><div className="v">{fmtTime(model.end?.ttfs as number | undefined ?? incs[0]?.t)}</div><div className="l">first solution</div></div>
      </div>

      <div className="panel">
        <div className="panel-head">
          <h2>Primal–dual bounds</h2>
          <span className="muted small">{model.method === "lns"
            ? "LNS: the lower bound is the root LP bound · ▲ greedy repair ◆ price-and-dive ■ MIP"
            : "shaded = primal heuristics running · ● node ◆ diving ■ MIP"}</span>
          <div className="spacer" />
          <div className="seg">
            <button className={xMode === "time" ? "on" : ""} onClick={() => setXMode("time")}>vs time</button>
            <button className={xMode === "nodes" ? "on" : ""} onClick={() => setXMode("nodes")}>vs nodes</button>
          </div>
        </div>
        {b.length ? <EChart option={boundsOption} height={330} /> : <div className="empty">Waiting for the first bound…</div>}
      </div>

      <div className="grid cols-2" style={{ marginTop: 14 }}>
        <div className="panel">
          <div className="panel-head"><h2>Optimality gap</h2><span className="muted small">(UB − LB) / LB, log scale</span></div>
          {b.some((p) => p.ub !== null) ? <EChart option={gapOption} height={260} /> : <div className="empty">No incumbent yet</div>}
        </div>
        <div className="panel">
          <div className="panel-head"><h2>Search effort</h2></div>
          {b.some((p) => p.pool !== null) ? <EChart option={poolOption} height={260} /> : <div className="empty">—</div>}
        </div>
      </div>

      {model.method === "lns" && <LnsPanel model={model} version={version} tSolve={tSolve} />}

      <div className="panel" style={{ marginTop: 14 }}>
        <div className="panel-head">
          <h2>Column generation</h2>
          <span className="muted small">RMP value decreases, Lagrangian bound increases: they meet at the LP optimum (tailing-off at the end)</span>
          <div className="spacer" />
          {cgGroups.length > 0 && (
            <select value={cgKey} onChange={(e) => setCgNode(e.target.value)}>
              {cgGroups.map(([k, g]) => (
                <option key={k} value={k}>
                  {g.ctx === "node" ? `B&P node ${g.node}` : g.ctx === "dive" ? `dive from node ${g.node}` : g.ctx === "root" ? "root (all trains)" : `LNS iteration ${g.node}`} — {g.n} iterations
                </option>
              ))}
            </select>
          )}
        </div>
        {cgSeries.length ? <EChart option={cgOption} height={380} /> : <div className="empty">{model.legacy ? "Not recorded by the old solver." : "No CG iteration at this time for this node."}</div>}
      </div>

      <div className="grid cols-2" style={{ marginTop: 14 }}>
        <div className="panel">
          <div className="panel-head"><h2>Where the time goes</h2></div>
          {prof.length ? <EChart option={profOption} height={260} /> : endProf ? (
            <EChart height={260} option={{
              tooltip: { trigger: "item", valueFormatter: (v) => fmtTime(v as number) },
              series: [{ type: "pie", radius: ["45%", "72%"], label: { color: "#cbd5e1" }, data: [
                { name: "master LP", value: endProf.master, itemStyle: { color: "#3b82f6" } },
                { name: "pricing", value: endProf.pricing, itemStyle: { color: "#22c55e" } },
                { name: "other", value: Math.max(0, endProf.total - endProf.master - endProf.pricing), itemStyle: { color: "#64748b" } },
              ] }],
            }} />
          ) : <div className="empty">—</div>}
        </div>
        <div className="panel">
          <div className="panel-head"><h2>Incumbents</h2><span className="muted small">each improvement of the best integer solution</span></div>
          {incs.length === 0 ? <div className="empty">{model.legacy ? "Not recorded by the old solver." : "None yet."}</div> : (
            <div style={{ maxHeight: 260, overflow: "auto" }}>
              <table className="data">
                <thead><tr><th>#</th><th className="num">time</th><th className="num">value</th><th className="num">improvement</th><th>found by</th></tr></thead>
                <tbody>
                  {incs.map((i, k) => (
                    <tr key={i.idx}>
                      <td>{i.idx + 1}</td><td className="num">{i.t.toFixed(2)} s</td><td className="num">{fmt(i.value)}</td>
                      <td className="num">{k ? `−${fmt(((incs[k - 1].value - i.value) / incs[k - 1].value) * 100, 2)} %` : "first"}</td>
                      <td>{sourceLabel(i.source)}</td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
          )}
        </div>
      </div>
    </>
  );
}
