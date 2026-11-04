import { useEffect, useMemo, useRef, useState } from "react";
import { METHOD_LABEL, api, type Batch, type Instance, type RunInfo, type Solution } from "../api";
import { fmt, fmtTime, trainColor } from "../colors";
import { href } from "../router";
import { kpisAt, stateAt } from "../sim";
import EChart from "./EChart";
import Gantt, { type GanttRow } from "./Gantt";
import PlayerBar, { usePlayback } from "./Playback";
import StationMap from "./StationMap";

interface Props {
  runs: RunInfo[];
  batches: Batch[];
  station: string | null;
  scenario: string | null;
  onInstance: (station: string, scenario: string) => void;
}

const SIDE_COLOR = ["#38bdf8", "#c084fc"];
const runLabel = (r: RunInfo, batches: Batch[]) => {
  const b = r.batch ? batches.find((x) => x.id === r.batch) : null;
  return `${METHOD_LABEL[r.method]}${r.link_conflicts ? "" : " (original model)"} — ${r.objective != null ? fmt(r.objective) : "no solution"}`
    + `${r.created ? ` · ${r.created.replace("T", " ").slice(5, 16)}` : r.kind === "legacy" ? " · old solver" : ""}`
    + `${b ? ` · ${b.name}` : ""}${r.params ? ` · ${Math.round(r.params.time_limit_s)} s` : ""}`;
};

/** Default pair: the B&P and LNS runs of the most recent benchmark of this instance;
 *  otherwise (per side) the best Branch-and-Price vs the best LNS run (corrected model first). */
function defaultPair(rs: RunInfo[], batches: Batch[]): [string | null, string | null] {
  const ok = rs.filter((r) => r.has_solution && r.objective != null);
  const okIds = new Set(ok.map((r) => r.id));
  const fromBatch = (m: string): RunInfo | undefined => {
    const sorted = [...batches].sort((a, b) => b.created.localeCompare(a.created));
    for (const b of sorted) {
      const job = b.jobs.find((j) => j.params.method === m && j.run_id && okIds.has(j.run_id) && rs.some((r) => r.id === j.run_id));
      if (job) return ok.find((r) => r.id === job.run_id);
    }
    return undefined;
  };
  const best = (m: string) =>
    ok.filter((r) => r.method === m).sort((a, b) => Number(b.link_conflicts) - Number(a.link_conflicts) || a.objective! - b.objective!)[0];
  const latestBatch = [...batches].sort((a, b) => b.created.localeCompare(a.created))
    .find((b) => b.jobs.some((j) => j.run_id && rs.some((r) => r.id === j.run_id)));
  const inLatest = (r?: RunInfo) => (r && latestBatch && r.batch === latestBatch.id ? r : undefined);
  const a = inLatest(fromBatch("bp")) ?? best("bp") ?? ok[0];
  const b = inLatest(fromBatch("lns")) ?? best("lns") ?? ok.find((r) => r.id !== a?.id);
  return [a?.id ?? null, b?.id ?? null];
}

/** Two runs of the same instance played side by side on the station map with one shared clock. */
export default function CompareSimulation({ runs, batches, station, scenario, onInstance }: Props) {
  const instances = useMemo(() => {
    const m = new Map<string, { station: string; scenario: string; n: number }>();
    for (const r of runs) {
      if (!r.station || !r.scenario || !(r.has_solution || r.state === "running")) continue;
      const k = `${r.station}|${r.scenario}`;
      m.set(k, { station: r.station, scenario: r.scenario, n: (m.get(k)?.n ?? 0) + 1 });
    }
    return [...m.values()].sort((a, b) => `${a.station}${a.scenario}`.localeCompare(`${b.station}${b.scenario}`));
  }, [runs]);

  const instRuns = useMemo(() => runs.filter((r) => r.station === station && r.scenario === scenario), [runs, station, scenario]);
  const [ids, setIds] = useState<[string | null, string | null]>([null, null]);
  useEffect(() => setIds(defaultPair(instRuns, batches)), [station, scenario]); // eslint-disable-line react-hooks/exhaustive-deps
  // when a run of the pair finishes (live benchmark), refresh the default choice
  const finishedKey = instRuns.filter((r) => r.has_solution).map((r) => r.id).join(",");
  const userPicked = useRef(false);
  useEffect(() => {
    // follow a benchmark that is finishing, unless the user chose the runs by hand
    if (!userPicked.current) setIds(defaultPair(instRuns, batches));
  }, [finishedKey, batches.length]); // eslint-disable-line react-hooks/exhaustive-deps
  useEffect(() => { userPicked.current = false; }, [station, scenario]);

  const [inst, setInst] = useState<Instance | null>(null);
  const [sols, setSols] = useState<[Solution | null, Solution | null]>([null, null]);
  const [err, setErr] = useState<string | null>(null);
  const [selected, setSelected] = useState<number | null>(null);

  useEffect(() => {
    setInst(null);
    if (station && scenario) api.instance(station, scenario).then(setInst).catch((e) => setErr(e.message));
  }, [station, scenario]);
  useEffect(() => {
    setErr(null);
    setSols([null, null]);
    ids.forEach((id, i) => {
      if (!id) return;
      api.solution(id)
        .then((s) => setSols((cur) => (i === 0 ? [s, cur[1]] : [cur[0], s])))
        .catch((e) => setErr(e.message));
    });
  }, [ids[0], ids[1], finishedKey]); // eslint-disable-line react-hooks/exhaustive-deps

  const T = Math.max(inst?.T ?? 0, sols[0]?.T ?? 0, sols[1]?.T ?? 0, 1);
  const pb = usePlayback(T, 0);
  const states = sols.map((s) => (s ? s.trains.map((tr) => stateAt(tr, pb.t)) : []));
  const kpis = sols.map((s, i) => (s ? kpisAt(states[i], s.trains, pb.t) : null));
  const infos = ids.map((id) => runs.find((r) => r.id === id) ?? null);

  // live benchmark of this instance
  const live = batches.flatMap((b) => b.jobs).filter((j) => j.station === station && j.scenario === scenario && (j.state === "running" || j.state === "pending"));

  const diff = useMemo(() => {
    if (!sols[0] || !sols[1]) return null;
    const refused = (s: Solution) => new Set(s.trains.filter((t) => t.rejected).map((t) => t.id));
    const ra = refused(sols[0]), rb = refused(sols[1]);
    const onlyA = [...ra].filter((k) => !rb.has(k));
    const onlyB = [...rb].filter((k) => !ra.has(k));
    const cost = (s: Solution) => new Map(s.trains.map((t) => [t.id, t.cost]));
    const ca = cost(sols[0]), cb = cost(sols[1]);
    const changed = [...ca.keys()].filter((k) => !ra.has(k) && !rb.has(k) && ca.get(k) !== cb.get(k))
      .map((k) => ({ id: k, a: ca.get(k)!, b: cb.get(k)! })).sort((x, y) => Math.abs(y.a - y.b) - Math.abs(x.a - x.b));
    return { onlyA, onlyB, changed };
  }, [sols]);

  const breakdown = useMemo(() => {
    const cats = ["shunting", "service", "storage", "dwelling", "rejection"];
    return cats.map((c) => sols.map((s) => (s ? s.trains.reduce((a, t) => a + (t.cost_breakdown[c] ?? 0), 0) : 0)));
  }, [sols]);

  const rows: GanttRow[] = useMemo(() => {
    if (!sols[0] || !sols[1]) return [];
    const byId = sols.map((s) => new Map(s!.trains.map((t) => [t.id, t])));
    const ids0 = [...byId[0].keys()].sort((a, b) => (byId[0].get(a)!.a_t - byId[0].get(b)!.a_t) || a - b);
    const tag = (i: number) => (infos[i] ? (infos[i]!.method === "lns" ? "LNS" : "B&P") : "?");
    return ids0.flatMap((k) => [0, 1].map((i) => {
      const t = byId[i].get(k)!;
      const differs = byId[0].get(k)!.rejected !== byId[1].get(k)?.rejected;
      return {
        id: k, key: `${k}-${i}`, label: `T${k} ${tag(i)}${i === 0 && differs ? " ≠" : ""}`, labelColor: SIDE_COLOR[i],
        a_t: t.a_t, d_t: t.d_t, segments: t.segments, rejected: t.rejected, separator: i === 0,
      };
    }));
  }, [sols, infos[0]?.method, infos[1]?.method]); // eslint-disable-line react-hooks/exhaustive-deps

  return (
    <div className="panel" id="side-by-side">
      <div className="panel-head">
        <h2>Watch the methods on the station</h2>
        <span className="muted small">two runs of the same instance, one shared clock · same colour = same train</span>
        <div className="spacer" />
        <select value={station && scenario ? `${station}|${scenario}` : ""} onChange={(e) => { const [s, c] = e.target.value.split("|"); onInstance(s, c); }}>
          <option value="" disabled>choose an instance…</option>
          {instances.map((x) => (
            <option key={`${x.station}|${x.scenario}`} value={`${x.station}|${x.scenario}`}>{x.station} · {x.scenario} ({x.n} runs)</option>
          ))}
        </select>
      </div>

      {!station && <div className="empty">Choose an instance that has been solved (or run a benchmark from an instance page with <b>Both</b>).</div>}
      {live.length > 0 && (
        <div className="banner info">
          <span className="spinner" />
          <div className="small">
            Benchmark in progress on this instance:{" "}
            {live.map((j, i) => (
              <span key={i} style={{ marginRight: 10 }}>
                {METHOD_LABEL[j.params.method]} {j.state === "running" && j.run_id ? <a href={href.run(j.run_id, "convergence")}>running — follow live →</a> : "pending"}
              </span>
            ))}
            The comparison fills in as soon as each run has a solution.
          </div>
        </div>
      )}
      {err && <div className="banner bad"><span>✕</span><div className="small">{err}</div></div>}

      {station && inst && (
        <>
          <div className="grid cols-2">
            {[0, 1].map((i) => {
              const info = infos[i];
              const s = sols[i];
              const k = kpis[i];
              const cross = s ? s.conflicts.filter((c) => c.kind === "link").length : 0;
              return (
                <div key={i} style={{ borderTop: `3px solid ${SIDE_COLOR[i]}`, paddingTop: 8 }}>
                  <div className="row" style={{ marginBottom: 8 }}>
                    <select value={ids[i] ?? ""} style={{ maxWidth: "100%" }}
                      onChange={(e) => { userPicked.current = true; const v = e.target.value; setIds((cur) => (i === 0 ? [v, cur[1]] : [cur[0], v])); }}>
                      <option value="" disabled>choose a run…</option>
                      {instRuns.filter((r) => r.has_solution).map((r) => <option key={r.id} value={r.id}>{runLabel(r, batches)}</option>)}
                    </select>
                    {info && <a className="small" href={href.run(info.id)}>open run →</a>}
                  </div>
                  {s ? (
                    <>
                      <div className="row small" style={{ marginBottom: 6, gap: 8 }}>
                        <span className="chip method">{info ? METHOD_LABEL[info.method] : "?"}</span>
                        <span>objective <b>{fmt(s.objective)}</b></span>
                        <span className="chip info">{s.accepted} accepted</span>
                        {(s.rejected ?? 0) > 0 && <span className="chip bad">{s.rejected} refused</span>}
                        {cross ? <span className="chip bad">⚠ {cross} crossings</span> : <span className="chip ok">✓ valid</span>}
                        {info?.time_s != null && <span className="muted">{fmtTime(info.time_s)}{info.gap != null ? ` · own gap ${fmt(info.gap, 1)} %` : ""}</span>}
                      </div>
                      <StationMap inst={inst} states={states[i]} selected={selected} onSelect={setSelected} height={360}
                        alerts={s.conflicts.filter((c) => c.kind === "link" && pb.t >= c.t && pb.t < c.t + inst.t_s).map((c) => ({ from: c.from!, to: c.to! }))} />
                      {k && (
                        <div className="kpis" style={{ marginTop: 8, gridTemplateColumns: "repeat(5, minmax(0, 1fr))" }}>
                          <div className="kpi"><div className="v">{k.present}</div><div className="l">in station</div></div>
                          <div className="kpi"><div className="v" style={{ color: "#f59e0b" }}>{k.moving}</div><div className="l">shunting</div></div>
                          <div className="kpi"><div className="v" style={{ color: "#22c55e" }}>{k.inService}</div><div className="l">in service</div></div>
                          <div className="kpi"><div className="v">{k.servicesDone}<span className="muted small">/{k.servicesRequired}</span></div><div className="l">services</div></div>
                          <div className="kpi"><div className="v">{fmt(k.costSoFar)}</div><div className="l">cost so far</div></div>
                        </div>
                      )}
                    </>
                  ) : (
                    <div className="empty">{ids[i] ? <span className="spinner" /> : "No run with a solution for this side yet."}</div>
                  )}
                </div>
              );
            })}
          </div>

          <PlayerBar t={pb.t} tMax={T} step={inst.t_s} playing={pb.playing} speed={pb.speed} speeds={[2, 5, 10, 30, 60]}
            onToggle={pb.toggle} onSeek={pb.setT} onSpeed={pb.setSpeed} format={(v) => `t = ${String(Math.floor(v)).padStart(3, "0")}`} />

          {diff && (
            <div className="grid cols-2">
              <div className="panel" style={{ background: "var(--panel-2)" }}>
                <h3 style={{ marginBottom: 8 }}>Where they differ</h3>
                <div className="small" style={{ lineHeight: 1.9 }}>
                  <div>
                    Objective: <b style={{ color: SIDE_COLOR[0] }}>{fmt(sols[0]!.objective)}</b> vs <b style={{ color: SIDE_COLOR[1] }}>{fmt(sols[1]!.objective)}</b>
                    {sols[0]!.objective != null && sols[1]!.objective != null && (
                      <span className="muted"> ({sols[1]!.objective! >= sols[0]!.objective! ? "+" : ""}{fmt(((sols[1]!.objective! - sols[0]!.objective!) / sols[0]!.objective!) * 100, 2)} % for the right run)</span>
                    )}
                  </div>
                  <div>Refused only on the left: {diff.onlyA.length ? diff.onlyA.map((k) => <TrainChip key={k} id={k} onClick={setSelected} />) : <span className="muted">none</span>}</div>
                  <div>Refused only on the right: {diff.onlyB.length ? diff.onlyB.map((k) => <TrainChip key={k} id={k} onClick={setSelected} />) : <span className="muted">none</span>}</div>
                  <div>Accepted by both, different cost: {diff.changed.length ? diff.changed.slice(0, 8).map((c) => (
                    <span key={c.id} style={{ marginRight: 8 }}><TrainChip id={c.id} onClick={setSelected} /> {fmt(c.a)} → {fmt(c.b)}</span>
                  )) : <span className="muted">none</span>}{diff.changed.length > 8 && <span className="muted"> +{diff.changed.length - 8} more</span>}</div>
                </div>
              </div>
              <div className="panel" style={{ background: "var(--panel-2)" }}>
                <h3 style={{ marginBottom: 4 }}>Cost breakdown</h3>
                <EChart height={200} option={{
                  grid: { left: 80, right: 20, top: 24, bottom: 24 },
                  legend: { top: 0, textStyle: { color: "#cbd5e1" } },
                  tooltip: { trigger: "axis", axisPointer: { type: "shadow" } },
                  xAxis: { type: "value", axisLine: { lineStyle: { color: "#33476e" } }, splitLine: { lineStyle: { color: "#16223b" } } },
                  yAxis: { type: "category", data: ["shunting", "service", "storage", "dwelling", "rejection"] },
                  series: [0, 1].map((i) => ({
                    name: infos[i] ? `${["left", "right"][i]} · ${METHOD_LABEL[infos[i]!.method]}` : ["left", "right"][i],
                    type: "bar" as const, color: SIDE_COLOR[i], data: breakdown.map((b) => b[i]),
                  })),
                }} />
              </div>
            </div>
          )}

          {rows.length > 0 && (
            <div style={{ marginTop: 14 }}>
              <div className="row" style={{ marginBottom: 6 }}>
                <h3>Schedules, train by train</h3>
                <span className="muted small"><span style={{ color: SIDE_COLOR[0] }}>left run</span> above <span style={{ color: SIDE_COLOR[1] }}>right run</span> · ≠ = accepted by one, refused by the other</span>
              </div>
              <Gantt inst={inst} T={T} t={pb.t} onSeek={pb.setT} selected={selected} onSelect={setSelected} rows={rows} rowHeight={15} />
            </div>
          )}
        </>
      )}
    </div>
  );
}

function TrainChip({ id, onClick }: { id: number; onClick: (id: number) => void }) {
  return (
    <span className="chip neutral" style={{ cursor: "pointer", marginRight: 4, borderColor: trainColor(id) }} onClick={() => onClick(id)}>
      <span style={{ color: trainColor(id) }}>●</span> T{id}
    </span>
  );
}
