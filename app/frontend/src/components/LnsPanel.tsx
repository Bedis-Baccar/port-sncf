import type { EChartsOption } from "echarts";
import { useMemo } from "react";
import { LNS_OPERATORS, LNS_REPAIRS } from "../api";
import { fmt, fmtTime } from "../colors";
import type { LnsIteration, TraceModel } from "../trace";
import { LNS_RESULT_COLOR, LNS_RESULT_LABEL } from "../trace";
import EChart from "./EChart";

const AX = { axisLine: { lineStyle: { color: "#33476e" } }, splitLine: { lineStyle: { color: "#16223b" } } };
const ALNS_RESULTS = ["new_best", "improved", "accepted", "rejected", "same", "pruned", "failed"];
const OLD_RESULTS = ["improved", "no_improvement", "dive_cut", "pruned", "mip_timeout"];
const OP_COLOR: Record<string, string> = {
  random: "#94a3b8", time: "#38bdf8", related: "#f472b6", dual: "#facc15", rejected: "#f97316", bottleneck: "#a78bfa",
  greedy: "#22d3ee", regret: "#fb7185", dive: "#4ade80", dive_mip: "#c084fc",
};
const opLabel = (id: string) => LNS_OPERATORS.find((o) => o.id === id)?.label ?? LNS_REPAIRS.find((o) => o.id === id)?.label ?? id;

/** Keeps at most `max` points, always keeping the interesting ones. */
function sample(iters: LnsIteration[], max: number, keep: (i: LnsIteration) => boolean) {
  if (iters.length <= max) return iters;
  const step = Math.ceil(iters.length / max);
  return iters.filter((it, idx) => keep(it) || idx % step === 0);
}

/** (A)LNS views: search trajectory, operator outcomes, adaptive weights, temperature, repair costs. */
export default function LnsPanel({ model, version, tSolve }: { model: TraceModel; version: number; tSolve: number }) {
  const iters = useMemo(() => model.lns.filter((i) => i.t <= tSolve), [model, version, tSolve]); // eslint-disable-line react-hooks/exhaustive-deps
  const alns = model.lns.some((i) => i.rep);
  const RESULTS = alns ? ALNS_RESULTS : OLD_RESULTS;
  const rootLb = (model.end?.root_lb as number | undefined) ?? model.bounds.find((b) => b.lb !== null)?.lb ?? null;
  const dops = useMemo(() => [...new Set(model.lns.map((i) => i.op))], [model, version]); // eslint-disable-line react-hooks/exhaustive-deps
  const rops = useMemo(() => [...new Set(model.lns.map((i) => i.rep).filter(Boolean) as string[])], [model, version]); // eslint-disable-line react-hooks/exhaustive-deps

  const count = (key: (i: LnsIteration) => string | null) => {
    const c: Record<string, Record<string, number>> = {};
    for (const i of iters) {
      const k = key(i);
      if (!k) continue;
      c[k] ??= {};
      c[k][i.result] = (c[k][i.result] ?? 0) + 1;
    }
    return c;
  };
  const byOp = useMemo(() => count((i) => i.op), [iters]); // eslint-disable-line react-hooks/exhaustive-deps
  const byRep = useMemo(() => count((i) => i.rep), [iters]); // eslint-disable-line react-hooks/exhaustive-deps
  const n = (r: string) => iters.filter((i) => i.result === r).length;
  const cut = alns ? n("pruned") : n("pruned") + n("dive_cut");
  const span = iters.length ? iters[iters.length - 1].t - (iters[0].t - iters[0].dur) : 0;
  const dives = iters.filter((i) => i.rows > 0);

  const trajOption: EChartsOption = useMemo(() => {
    const pts = sample(iters, 3000, (i) => i.result !== "rejected" && i.result !== "same");
    const xMax = Math.max(model.tMax, 1e-3);
    return {
      grid: { left: 70, right: 20, top: 34, bottom: 40 },
      legend: { top: 2, textStyle: { color: "#cbd5e1" }, type: "scroll" },
      tooltip: {
        trigger: "item",
        formatter: (p: any) => {
          const i: LnsIteration | undefined = p.data?.it;
          if (!i) return `${p.seriesName}: ${fmt(p.value?.[1], 1)}`;
          return `iteration ${i.it} · ${opLabel(i.op)}${i.rep ? ` + ${opLabel(i.rep)}` : ""} · k=${i.k}`
            + `<br/>candidate <b>${fmt(i.value)}</b>${i.lb != null ? ` · bound ${fmt(i.lb, 1)}` : ""}`
            + `<br/>${LNS_RESULT_LABEL[i.result] ?? i.result}${i.thr != null ? ` · threshold ${fmt(i.thr)}` : ""}`
            + `<br/>${i.dur < 1 ? `${(i.dur * 1000).toFixed(1)} ms` : `${i.dur.toFixed(2)} s`}${i.rows ? ` · sub-master ${i.rows}×${i.cols}` : ""}`;
        },
      },
      xAxis: { type: "value", name: "solver time (s)", nameLocation: "middle", nameGap: 26, min: 0, max: xMax, ...AX },
      yAxis: { type: "value", scale: true, name: "objective", ...AX },
      series: [
        ...RESULTS.filter((r) => r !== "same").map((r) => ({
          name: LNS_RESULT_LABEL[r], type: "scatter" as const, symbolSize: r === "new_best" ? 9 : 4, large: true,
          color: LNS_RESULT_COLOR[r], z: r === "new_best" ? 5 : 2,
          data: pts.filter((i) => i.result === r && (alns ? i.value != null : i.lb != null))
            .map((i) => ({ value: [i.t, alns ? i.value! : i.lb!], it: i })),
        })),
        ...(alns ? [{ name: "current solution", type: "line" as const, step: "end" as const, symbol: "none", color: "#f59e0b",
          lineStyle: { width: 1.2 }, data: sample(iters, 3000, () => false).map((i) => [i.t, i.cur]) }] : []),
        { name: "best solution (UB)", type: "line" as const, step: "end" as const, symbol: "none", color: "#22c55e", lineStyle: { width: 2.2 },
          data: model.bounds.filter((b) => b.ub !== null && b.t <= tSolve).map((b) => [b.t, b.ub]) },
        ...(rootLb !== null ? [{ name: "root LP bound", type: "line" as const, symbol: "none", color: "#94a3b8", lineStyle: { type: "dotted" as const },
          data: [[0, rootLb], [xMax, rootLb]] }] : []),
      ],
    };
  }, [iters, model, tSolve, rootLb, alns]); // eslint-disable-line react-hooks/exhaustive-deps

  const outcomeOption = (ops: string[], c: Record<string, Record<string, number>>): EChartsOption => ({
    grid: { left: 100, right: 20, top: 30, bottom: 30 },
    legend: { top: 0, textStyle: { color: "#cbd5e1", fontSize: 10 }, type: "scroll" },
    tooltip: { trigger: "axis", axisPointer: { type: "shadow" } },
    xAxis: { type: "value", name: "iterations", ...AX },
    yAxis: { type: "category", data: ops.map(opLabel), ...AX },
    series: RESULTS.map((r) => ({
      name: LNS_RESULT_LABEL[r] ?? r, type: "bar" as const, stack: "r", color: LNS_RESULT_COLOR[r],
      data: ops.map((o) => c[o]?.[r] ?? 0),
    })),
  });

  const adaptOption: EChartsOption = useMemo(() => {
    const pts = sample(iters, 800, () => false);
    const grids = alns ? 3 : 2;
    const h = (100 - 14) / grids;
    const top = (g: number) => `${8 + g * h}%`;
    const xAxis = Array.from({ length: grids }, (_, g) => ({
      type: "value" as const, gridIndex: g, min: 0, max: Math.max(1, model.lns.length),
      axisLabel: { show: g === grids - 1 }, name: g === grids - 1 ? "iteration" : "", nameLocation: "middle" as const, nameGap: 22, ...AX,
    }));
    return {
      grid: Array.from({ length: grids }, (_, g) => ({ left: 60, right: 60, top: top(g), height: `${h - 7}%` })),
      legend: { top: 0, textStyle: { color: "#cbd5e1", fontSize: 10 }, type: "scroll" },
      tooltip: { trigger: "axis" },
      xAxis,
      yAxis: [
        { type: "value", gridIndex: 0, name: "destroy w", ...AX },
        ...(alns ? [{ type: "value" as const, gridIndex: 1, name: "repair w", ...AX }] : []),
        { type: "value", gridIndex: grids - 1, name: "k", minInterval: 1, ...AX },
        ...(alns ? [{ type: "value" as const, gridIndex: grids - 1, name: "T (% of cur.)", position: "right" as const, ...AX, splitLine: { show: false } }] : []),
      ],
      series: [
        ...dops.map((o) => ({ name: opLabel(o), type: "line" as const, xAxisIndex: 0, yAxisIndex: 0, symbol: "none", color: OP_COLOR[o],
          data: pts.map((i) => [i.it, i.weights[o] ?? null]) })),
        ...rops.map((o) => ({ name: opLabel(o), type: "line" as const, xAxisIndex: 1, yAxisIndex: 1, symbol: "none", color: OP_COLOR[o],
          lineStyle: { width: 2 }, data: pts.map((i) => [i.it, i.rweights[o] ?? null]) })),
        { name: "k (trains freed)", type: "line" as const, step: "end" as const, xAxisIndex: grids - 1, yAxisIndex: alns ? 2 : 1, symbol: "none",
          color: "#a78bfa", areaStyle: { opacity: 0.12 }, data: pts.map((i) => [i.it, i.k]) },
        ...(alns ? [{ name: "temperature", type: "line" as const, xAxisIndex: 2, yAxisIndex: 3, symbol: "none", color: "#f59e0b",
          lineStyle: { type: "dashed" as const }, data: pts.map((i) => [i.it, i.temp != null && i.cur ? (100 * i.temp * Math.LN2) / i.cur : null]) }] : []),
      ],
    };
  }, [iters, dops, rops, alns, model.lns.length]);

  const repairRows = useMemo(() => {
    const total = iters.reduce((a, i) => a + i.dur, 0) || 1;
    return rops.map((r) => {
      const its = iters.filter((i) => i.rep === r);
      const time = its.reduce((a, i) => a + i.dur, 0);
      const c = (x: string) => its.filter((i) => i.result === x).length;
      return { r, tries: its.length, share: (100 * time) / total, ms: its.length ? (1000 * time) / its.length : 0,
        best: c("new_best"), improved: c("improved"), accepted: c("accepted"), pruned: c("pruned"),
        w: its.length ? its[its.length - 1].rweights[r] : null };
    });
  }, [iters, rops]);

  if (!model.lns.length && !model.end) {
    return <div className="panel" style={{ marginTop: 14 }}><div className="empty">The LNS starts after the root column generation…</div></div>;
  }

  return (
    <>
      <div className="panel" style={{ marginTop: 14 }}>
        <div className="panel-head">
          <h2>{alns ? "ALNS search" : "LNS neighbourhoods"}</h2>
          <span className="muted small">{alns
            ? "each point = one repaired candidate; amber = current solution (simulated annealing), green = best"
            : "each point = one neighbourhood at its Lagrangian bound; above UB − 1 it cannot improve"}</span>
        </div>
        <div className="kpis" style={{ marginBottom: 10 }}>
          <div className="kpi"><div className="v">{fmt(iters.length)}</div><div className="l">iterations</div></div>
          <div className="kpi"><div className="v">{span > 0 ? fmt(iters.length / span, 0) : "—"}</div><div className="l">iterations / s</div></div>
          <div className="kpi"><div className="v" style={{ color: LNS_RESULT_COLOR.new_best }}>{alns ? n("new_best") : n("improved")}</div><div className="l">new best</div></div>
          {alns && <div className="kpi"><div className="v" style={{ color: LNS_RESULT_COLOR.accepted }}>{fmt(n("accepted"))}</div><div className="l">worse moves accepted</div></div>}
          <div className="kpi"><div className="v" style={{ color: LNS_RESULT_COLOR.pruned }}>{iters.length ? fmt((100 * cut) / iters.length, 0) : 0} %</div><div className="l">cut by a bound</div></div>
          {dives.length > 0 && <div className="kpi"><div className="v">{fmt(dives.reduce((a, i) => a + i.rows, 0) / dives.length, 0)}<span className="muted small"> × {fmt(dives.reduce((a, i) => a + i.cols, 0) / dives.length, 0)}</span></div><div className="l">sub-master rows × cols</div></div>}
          <div className="kpi"><div className="v">{fmtTime(iters.length ? span / iters.length : null)}</div><div className="l">per iteration</div></div>
        </div>
        <EChart option={trajOption} height={320} />
      </div>
      <div className="grid cols-2" style={{ marginTop: 14 }}>
        <div className="panel">
          <div className="panel-head"><h2>Destroy operators</h2><span className="muted small">outcome of the iterations using each operator</span></div>
          {dops.length ? <EChart option={outcomeOption(dops, byOp)} height={230} /> : <div className="empty">—</div>}
          {alns && rops.length > 0 && (
            <>
              <div className="panel-head" style={{ marginTop: 6 }}><h2>Repair operators</h2></div>
              <EChart option={outcomeOption(rops, byRep)} height={190} />
              <table className="data small">
                <thead><tr><th>repair</th><th className="num">tries</th><th className="num">time share</th><th className="num">per try</th>
                  <th className="num">best</th><th className="num">better</th><th className="num">accepted</th><th className="num">pruned</th><th className="num">weight</th></tr></thead>
                <tbody>
                  {repairRows.map((x) => (
                    <tr key={x.r}>
                      <td><span style={{ color: OP_COLOR[x.r] }}>●</span> {opLabel(x.r)}</td>
                      <td className="num">{fmt(x.tries)}</td><td className="num">{fmt(x.share, 1)} %</td>
                      <td className="num">{x.ms < 1000 ? `${fmt(x.ms, 1)} ms` : `${fmt(x.ms / 1000, 2)} s`}</td>
                      <td className="num">{x.best}</td><td className="num">{x.improved}</td><td className="num">{fmt(x.accepted)}</td>
                      <td className="num">{fmt(x.pruned)}</td><td className="num">{fmt(x.w, 2)}</td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </>
          )}
        </div>
        <div className="panel">
          <div className="panel-head"><h2>Adaptation</h2><span className="muted small">{alns
            ? "segment-wise weights (destroy: score per use, repair: score per second), k and temperature"
            : "operator weights and neighbourhood size"}</span></div>
          {iters.length ? <EChart option={adaptOption} height={alns ? 470 : 260} /> : <div className="empty">—</div>}
        </div>
      </div>
    </>
  );
}
