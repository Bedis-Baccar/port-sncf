import { useMemo, useState } from "react";
import type { Instance } from "../api";
import { fmt } from "../colors";
import BBTree from "../components/BBTree";
import type { TraceModel } from "../trace";
import { STATUS_COLOR, nodeStatusAt } from "../trace";

export default function TreeTab({ model, version, tSolve, inst }: { model: TraceModel; version: number; tSolve: number; inst: Instance | null }) {
  const [selected, setSelected] = useState<number | null>(null);

  const counts = useMemo(() => {
    const c: Record<string, number> = {};
    let maxDepth = 0;
    for (const n of model.nodes.values()) {
      const st = nodeStatusAt(n, tSolve);
      if (!st) continue;
      c[st.status] = (c[st.status] ?? 0) + 1;
      maxDepth = Math.max(maxDepth, n.depth);
    }
    return { c, maxDepth };
  }, [model, version, tSolve]); // eslint-disable-line react-hooks/exhaustive-deps

  const current = useMemo(() => {
    let cur: number | null = null;
    for (const b of model.bounds) {
      if (b.t <= tSolve) cur = b.node;
      else break;
    }
    return cur;
  }, [model, version, tSolve]); // eslint-disable-line react-hooks/exhaustive-deps

  const sel = selected !== null ? model.nodes.get(selected) : null;
  const svcName = (label: string) => label.replace(/S(\d+)/, (_, s) => inst?.services[+s]?.name ?? `S${s}`);

  if (model.legacy) {
    return <div className="banner info"><span>ℹ</span><div className="small">The search tree is only recorded by the new solver. Re-run this instance to see it.</div></div>;
  }

  return (
    <div className="grid sim">
      <div className="panel">
        <div className="panel-head">
          <h2>Branch-and-price tree</h2>
          <span className="muted small">dashed ring = node being solved · scroll to zoom</span>
        </div>
        <BBTree nodes={model.nodes} t={tSolve} current={current} selected={selected} onSelect={setSelected} height={560} />
      </div>
      <div className="panel" style={{ display: "flex", flexDirection: "column", gap: 12 }}>
        <div className="kpis" style={{ gridTemplateColumns: "1fr 1fr" }}>
          {Object.entries(STATUS_COLOR).map(([k, c]) => (
            <div className="kpi" key={k}><div className="v" style={{ color: c }}>{counts.c[k] ?? 0}</div><div className="l">{k.toLowerCase()}</div></div>
          ))}
        </div>
        <div className="muted small">max depth {counts.maxDepth}{model.strategy.length ? ` · strategy switches: ${model.strategy.map((s) => `${s.value} @${s.t.toFixed(1)}s`).join(", ")}` : ""}</div>
        {sel ? (
          <div className="panel" style={{ background: "var(--panel-2)" }}>
            <div className="row"><b>Node {sel.id}</b><span className="muted">depth {sel.depth}{sel.id ? ` · child of ${sel.parent}` : ""}</span>
              <button className="ghost right" onClick={() => setSelected(null)}>✕</button></div>
            {sel.label && sel.id !== 0 && <div style={{ margin: "8px 0" }}>Branching: <code>{svcName(sel.label)}</code> {sel.rule && <span className="chip neutral">{sel.rule}</span>}</div>}
            <table className="data small">
              <thead><tr><th className="num">t (s)</th><th>status</th><th className="num">LB</th></tr></thead>
              <tbody>
                {sel.history.map((h, i) => (
                  <tr key={i} style={{ opacity: h.t <= tSolve ? 1 : 0.35 }}>
                    <td className="num">{h.t.toFixed(2)}</td>
                    <td><span style={{ color: STATUS_COLOR[h.status] }}>●</span> {h.status.toLowerCase()}</td>
                    <td className="num">{fmt(h.lb, 2)}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        ) : <div className="muted small">Click a node to see its branching decision and bound history.</div>}
        <div className="hint">
          <b>service split</b>: the train must start the service before / after a split time.
          <b> quantum</b>: at the first node where the fractional paths of a train diverge, the outgoing arcs are split in two balanced sets.
        </div>
      </div>
    </div>
  );
}
