import type { EChartsOption } from "echarts";
import { useMemo } from "react";
import { LNS_OPERATORS } from "../api";
import { fmt, fmtTime } from "../colors";
import type { TraceModel } from "../trace";
import { LNS_RESULT_COLOR } from "../trace";
import EChart from "./EChart";

const AX = { axisLine: { lineStyle: { color: "#33476e" } }, splitLine: { lineStyle: { color: "#16223b" } } };
const RESULTS = ["improved", "no_improvement", "dive_cut", "pruned", "mip_timeout"] as const;
const RESULT_LABEL: Record<string, string> = {
  improved: "improved the best solution",
  no_improvement: "repaired, no improvement",
  dive_cut: "dive cut by its bound",
  pruned: "pruned by the bound (no repair)",
  mip_timeout: "MIP polish timed out",
};
const OP_COLOR: Record<string, string> = { random: "#94a3b8", time: "#38bdf8", related: "#f472b6", dual: "#facc15", rejected: "#f97316" };
const opLabel = (id: string) => LNS_OPERATORS.find((o) => o.id === id)?.label ?? id;

/** LNS-specific views: neighbourhood bounds vs UB, operator statistics, adaptive weights and sizes. */
export default function LnsPanel({ model, version, tSolve }: { model: TraceModel; version: number; tSolve: number }) {
  const iters = useMemo(() => model.lns.filter((i) => i.t <= tSolve), [model, version, tSolve]); // eslint-disable-line react-hooks/exhaustive-deps
  const rootLb = (model.end?.root_lb as number | undefined) ?? model.bounds.find((b) => b.lb !== null)?.lb ?? null;
  const ops = useMemo(() => [...new Set(model.lns.map((i) => i.op))], [model, version]); // eslint-disable-line react-hooks/exhaustive-deps

  const counts = useMemo(() => {
    const c: Record<string, Record<string, number>> = {};
    for (const i of iters) {
      c[i.op] ??= {};
      c[i.op][i.result] = (c[i.op][i.result] ?? 0) + 1;
    }
    return c;
  }, [iters]);
  const totals = RESULTS.map((r) => iters.filter((i) => i.result === r).length);
  const pruneShare = iters.length ? (100 * (totals[2] + totals[3])) / iters.length : 0;
  const avgDur = iters.length ? iters.reduce((a, i) => a + i.dur, 0) / iters.length : 0;

  const scatterOption: EChartsOption = useMemo(() => ({
    grid: { left: 70, right: 20, top: 34, bottom: 40 },
    legend: { top: 2, textStyle: { color: "#cbd5e1" }, type: "scroll" },
    tooltip: {
      trigger: "item",
      formatter: (p: any) => {
        const i = p.data?.it;
        if (!i) return `${p.seriesName}: ${fmt(p.value?.[1], 1)}`;
        return `iteration ${i.it} · ${opLabel(i.op)} · k=${i.k}<br/>neighbourhood bound <b>${fmt(i.lb, 1)}</b>`
          + `<br/>${RESULT_LABEL[i.result] ?? i.result}${i.value != null ? ` → ${fmt(i.value)}` : ""}<br/>${i.cg_it} CG it., ${i.dive_steps} dive steps, ${i.dur.toFixed(2)} s`;
      },
    },
    xAxis: { type: "value", name: "solver time (s)", nameLocation: "middle", nameGap: 26, min: 0, max: Math.max(model.tMax, 1e-3), ...AX },
    yAxis: { type: "value", scale: true, name: "objective", ...AX },
    series: [
      ...RESULTS.map((r) => ({
        name: RESULT_LABEL[r], type: "scatter" as const, symbolSize: 7, color: LNS_RESULT_COLOR[r],
        data: iters.filter((i) => i.result === r && i.lb !== null).map((i) => ({ value: [i.t, i.lb], it: i })),
      })),
      { name: "best solution (UB)", type: "line" as const, step: "end" as const, symbol: "none", color: "#22c55e",
        data: model.bounds.filter((b) => b.ub !== null && b.t <= tSolve).map((b) => [b.t, b.ub]) },
      ...(rootLb !== null ? [{ name: "root LP bound", type: "line" as const, symbol: "none", color: "#94a3b8", lineStyle: { type: "dotted" as const },
        data: [[0, rootLb], [Math.max(model.tMax, 1e-3), rootLb]] }] : []),
    ],
  }), [iters, model, tSolve, rootLb]);

  const opsOption: EChartsOption = useMemo(() => ({
    grid: { left: 90, right: 20, top: 30, bottom: 30 },
    legend: { top: 0, textStyle: { color: "#cbd5e1", fontSize: 10 } },
    tooltip: { trigger: "axis", axisPointer: { type: "shadow" } },
    xAxis: { type: "value", name: "iterations", ...AX },
    yAxis: { type: "category", data: ops.map(opLabel), ...AX },
    series: RESULTS.map((r) => ({
      name: r.replace("_", " "), type: "bar" as const, stack: "r", color: LNS_RESULT_COLOR[r],
      data: ops.map((o) => counts[o]?.[r] ?? 0),
    })),
  }), [ops, counts]);

  const weightsOption: EChartsOption = useMemo(() => ({
    grid: [{ left: 60, right: 50, top: 30, height: "38%" }, { left: 60, right: 50, top: "64%", bottom: 34 }],
    legend: { top: 0, textStyle: { color: "#cbd5e1", fontSize: 10 } },
    tooltip: { trigger: "axis" },
    xAxis: [
      { type: "value", gridIndex: 0, min: 0, max: Math.max(1, model.lns.length), axisLabel: { show: false }, ...AX },
      { type: "value", gridIndex: 1, min: 0, max: Math.max(1, model.lns.length), name: "iteration", nameLocation: "middle", nameGap: 22, ...AX },
    ],
    yAxis: [
      { type: "value", gridIndex: 0, name: "weight", ...AX },
      { type: "value", gridIndex: 1, name: "trains freed", minInterval: 1, ...AX },
    ],
    series: [
      ...ops.map((o) => ({
        name: opLabel(o), type: "line" as const, xAxisIndex: 0, yAxisIndex: 0, symbol: "none", color: OP_COLOR[o],
        data: iters.map((i) => [i.it, i.weights[o] ?? null]),
      })),
      { name: "k", type: "line" as const, step: "end" as const, xAxisIndex: 1, yAxisIndex: 1, symbol: "none", color: "#a78bfa",
        areaStyle: { opacity: 0.15 }, data: iters.map((i) => [i.it, i.k]) },
    ],
  }), [ops, iters, model.lns.length]);

  if (!model.lns.length && !model.end) {
    return <div className="panel" style={{ marginTop: 14 }}><div className="empty">The LNS starts after the root column generation…</div></div>;
  }

  return (
    <>
      <div className="panel" style={{ marginTop: 14 }}>
        <div className="panel-head">
          <h2>LNS neighbourhoods</h2>
          <span className="muted small">each point = one neighbourhood, at its Lagrangian bound; above UB − 1 it cannot improve and is pruned</span>
        </div>
        <div className="kpis" style={{ marginBottom: 10 }}>
          <div className="kpi"><div className="v">{iters.length}</div><div className="l">iterations</div></div>
          <div className="kpi"><div className="v" style={{ color: LNS_RESULT_COLOR.improved }}>{totals[0]}</div><div className="l">improvements</div></div>
          <div className="kpi"><div className="v" style={{ color: LNS_RESULT_COLOR.pruned }}>{fmt(pruneShare, 0)} %</div><div className="l">cut by the bound</div></div>
          <div className="kpi"><div className="v">{fmtTime(avgDur)}</div><div className="l">per iteration</div></div>
          <div className="kpi"><div className="v">{fmt((model.end?.t_greedy as number | undefined) ?? null, 2)}</div><div className="l">greedy time (s)</div></div>
        </div>
        <EChart option={scatterOption} height={300} />
      </div>
      <div className="grid cols-2" style={{ marginTop: 14 }}>
        <div className="panel">
          <div className="panel-head"><h2>Destroy operators</h2><span className="muted small">outcome of each operator's neighbourhoods</span></div>
          {ops.length ? <EChart option={opsOption} height={240} /> : <div className="empty">—</div>}
        </div>
        <div className="panel">
          <div className="panel-head"><h2>Adaptation</h2><span className="muted small">operator weights (ALNS) and neighbourhood size</span></div>
          {iters.length ? <EChart option={weightsOption} height={240} /> : <div className="empty">—</div>}
        </div>
      </div>
    </>
  );
}
