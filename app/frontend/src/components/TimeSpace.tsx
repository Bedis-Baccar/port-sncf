import * as d3 from "d3";
import { useMemo, useRef, useState } from "react";
import type { Conflict, Instance, SolvedTrain } from "../api";
import { serviceColor, trainColor } from "../colors";
import { useWidth } from "../useWidth";

interface Props {
  inst: Instance;
  trains: SolvedTrain[];
  T: number;
  t: number;
  onSeek?: (t: number) => void;
  selected?: number | null;
  onSelect?: (id: number | null) => void;
  height?: number;
  crossings?: Conflict[];
}

const M = { l: 62, r: 14, t: 12, b: 28 };

/** Time-space diagram: x = time, y = physical node, one polyline per train path. */
export default function TimeSpace({ inst, trains, T, t, onSeek, selected = null, onSelect, height, crossings = [] }: Props) {
  const ref = useRef<SVGSVGElement>(null);
  const [wrapRef, width] = useWidth<HTMLDivElement>();
  const [hover, setHover] = useState<{ x: number; y: number; text: string } | null>(null);

  // nodes ordered as on the map: by layer (x), then vertical position
  const order = useMemo(() => {
    return [...inst.nodes]
      .sort((a, b) => {
        const pa = inst.layout[a.name], pb = inst.layout[b.name];
        return pa.x - pb.x || pa.y - pb.y;
      })
      .map((n) => n.name);
  }, [inst]);

  const H = height ?? Math.max(260, order.length * 13 + M.t + M.b);
  const x = d3.scaleLinear().domain([0, T]).range([M.l, width - M.r]);
  const y = d3.scalePoint<string>().domain(order).range([M.t + 4, H - M.b - 4]);

  const paths = useMemo(() => {
    return trains
      .filter((tr) => !tr.rejected)
      .map((tr, i) => {
        const pts: [number, number][] = [];
        const stays: { t0: number; t1: number; node: string; kind: string; service?: string }[] = [];
        for (const s of tr.segments) {
          if (s.kind === "move") {
            pts.push([s.t0, y(s.from!)!], [s.t1, y(s.to!)!]);
          } else if (s.kind === "arrive" || s.kind === "depart") {
            pts.push([s.t0, y(s.node!)!]);
          } else if (s.node) {
            pts.push([s.t0, y(s.node)!], [s.t1, y(s.node)!]);
            stays.push({ t0: s.t0, t1: s.t1, node: s.node, kind: s.kind, service: s.service });
          }
        }
        return { tr, pts, stays, i };
      });
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [trains, order, H]);

  const line = d3.line<[number, number]>().x((d) => x(d[0])).y((d) => d[1]);
  const ticks = x.ticks(Math.max(4, Math.floor(width / 90)));

  return (
    <div className="svg-wrap" style={{ background: "#0b1324" }} ref={wrapRef}>
      <svg ref={ref} width={width} height={H}
        onClick={(ev) => {
          const r = ref.current!.getBoundingClientRect();
          const tt = x.invert(ev.clientX - r.left);
          if (tt >= 0 && tt <= T) onSeek?.(Math.round(tt / inst.t_s) * inst.t_s);
        }}>
        {/* grid */}
        {ticks.map((v) => (
          <g key={v}>
            <line x1={x(v)} x2={x(v)} y1={M.t} y2={H - M.b} stroke="#1a2742" />
            <text x={x(v)} y={H - 10} fontSize={10.5} textAnchor="middle" fill="#7f8fab">{v}</text>
          </g>
        ))}
        {order.map((n) => (
          <g key={n}>
            <line x1={M.l} x2={width - M.r} y1={y(n)} y2={y(n)} stroke="#141f36" />
            <text x={M.l - 6} y={y(n)! + 3.5} fontSize={10} textAnchor="end" fill="#7f8fab">{n}</text>
          </g>
        ))}
        {/* train paths */}
        {paths.map(({ tr, pts, stays, i }) => {
          const sel = selected === tr.id;
          const dim = selected !== null && !sel;
          const flow = tr.flow ?? 1;
          const c = trainColor(tr.id);
          return (
            <g key={`${tr.id}-${i}`} opacity={(dim ? 0.12 : 1) * (0.25 + 0.75 * flow)}
              onClick={(ev) => { ev.stopPropagation(); onSelect?.(sel ? null : tr.id); }}
              onMouseMove={(ev) => setHover({ x: ev.nativeEvent.offsetX, y: ev.nativeEvent.offsetY, text: `T${tr.id} (${tr.name}) · ${tr.type}${flow < 0.999 ? ` · flow ${flow.toFixed(2)}` : ""} · cost ${tr.cost}` })}
              onMouseLeave={() => setHover(null)}
              style={{ cursor: "pointer" }}>
              <path d={line(pts) ?? ""} fill="none" stroke={c} strokeWidth={sel ? 3 : 1.6} strokeDasharray={flow < 0.999 ? "4 3" : undefined} />
              {stays.filter((s) => s.kind === "service").map((s, k) => (
                <rect key={k} x={x(s.t0)} y={y(s.node)! - 4} width={Math.max(1, x(s.t1) - x(s.t0))} height={8} rx={2}
                  fill={serviceColor(inst, s.service)} stroke={c} strokeWidth={1} />
              ))}
              {stays.filter((s) => s.kind === "storage").map((s, k) => (
                <rect key={`s${k}`} x={x(s.t0)} y={y(s.node)! - 3} width={Math.max(1, x(s.t1) - x(s.t0))} height={6} rx={2}
                  fill="#3b82f6" fillOpacity={0.6} />
              ))}
            </g>
          );
        })}
        {/* head-on crossings: an X between the two nodes, in the middle of the move */}
        {crossings.map((c, i) => {
          const ya = y(c.from!), yb = y(c.to!);
          if (ya === undefined || yb === undefined) return null;
          const cx = x(c.t + inst.t_s / 2), cy = (ya + yb) / 2;
          return (
            <g key={`x${i}`}>
              <circle cx={cx} cy={cy} r={9} fill="rgba(239,68,68,0.25)" stroke="#ef4444" />
              <path d={`M${cx - 5},${cy - 5} L${cx + 5},${cy + 5} M${cx - 5},${cy + 5} L${cx + 5},${cy - 5}`} stroke="#fca5a5" strokeWidth={2} />
            </g>
          );
        })}
        {/* time cursor */}
        <line x1={x(t)} x2={x(t)} y1={M.t} y2={H - M.b} stroke="#f8fafc" strokeWidth={1.4} strokeDasharray="3 3" />
        <text x={x(t) + 4} y={M.t + 10} fontSize={11} fill="#f8fafc">t={Math.round(t)}</text>
      </svg>
      {hover && <div className="tooltip" style={{ left: hover.x + 12, top: hover.y + 8 }}>{hover.text}</div>}
    </div>
  );
}
