import * as d3 from "d3";
import { useState } from "react";
import type { Instance, Segment } from "../api";
import { ACTIVITY_COLOR, serviceColor, trainColor } from "../colors";
import { useWidth } from "../useWidth";

export interface GanttRow {
  id: number;
  label: string;
  a_t: number;
  d_t: number;
  segments: Segment[];
  rejected?: boolean;
  chips?: string[];
}

interface Props {
  inst: Instance;
  rows: GanttRow[];
  T: number;
  t?: number;
  onSeek?: (t: number) => void;
  selected?: number | null;
  onSelect?: (id: number | null) => void;
  rowHeight?: number;
}

const M = { l: 96, r: 14, t: 8, b: 26 };

/** One row per train: time window [a_t, d_t] + activity bars. */
export default function Gantt({ inst, rows, T, t, onSeek, selected = null, onSelect, rowHeight = 18 }: Props) {
  const [wrapRef, width] = useWidth<HTMLDivElement>();
  const [hover, setHover] = useState<{ x: number; y: number; text: string } | null>(null);
  const H = M.t + M.b + rows.length * rowHeight;
  const x = d3.scaleLinear().domain([0, T]).range([M.l, width - M.r]);
  const ticks = x.ticks(Math.max(4, Math.floor(width / 90)));

  const segColor = (s: Segment) => (s.kind === "service" ? serviceColor(inst, s.service) : ACTIVITY_COLOR[s.kind] ?? "#475569");

  return (
    <div className="svg-wrap" style={{ background: "#0b1324" }} ref={wrapRef}>
      <svg width={width} height={H}
        onClick={(ev) => {
          const r = (ev.currentTarget as SVGSVGElement).getBoundingClientRect();
          const tt = x.invert(ev.clientX - r.left);
          if (tt >= 0 && tt <= T) onSeek?.(Math.round(tt / inst.t_s) * inst.t_s);
        }}>
        {ticks.map((v) => (
          <g key={v}>
            <line x1={x(v)} x2={x(v)} y1={M.t} y2={H - M.b} stroke="#16223b" />
            <text x={x(v)} y={H - 9} fontSize={10.5} textAnchor="middle" fill="#7f8fab">{v}</text>
          </g>
        ))}
        {rows.map((r, i) => {
          const y0 = M.t + i * rowHeight;
          const sel = selected === r.id;
          const dim = selected !== null && !sel;
          return (
            <g key={r.id} opacity={dim ? 0.35 : 1} style={{ cursor: onSelect ? "pointer" : "default" }}
              onClick={(ev) => { if (onSelect) { ev.stopPropagation(); onSelect(sel ? null : r.id); } }}>
              {sel && <rect x={0} y={y0} width={width} height={rowHeight} fill="rgba(59,130,246,0.12)" />}
              <rect x={6} y={y0 + rowHeight / 2 - 4} width={8} height={8} rx={2} fill={trainColor(r.id)} />
              <text x={18} y={y0 + rowHeight / 2 + 4} fontSize={11} fill={r.rejected ? "#fca5a5" : "#cbd5e1"}>{r.label}</text>
              {/* stay window */}
              <rect x={x(r.a_t)} y={y0 + 2} width={Math.max(1, x(r.d_t) - x(r.a_t))} height={rowHeight - 4} rx={3}
                fill={r.rejected ? "url(#hatch)" : "none"} stroke={r.rejected ? "#ef4444" : "#334766"} strokeDasharray={r.rejected ? undefined : "3 2"} />
              {r.segments.filter((s) => s.t1 > s.t0 && s.kind !== "rejected").map((s, k) => (
                <rect key={k} x={x(s.t0)} y={y0 + (s.kind === "move" ? 6 : 3)} width={Math.max(1, x(s.t1) - x(s.t0))}
                  height={s.kind === "move" ? rowHeight - 12 : rowHeight - 6} rx={2} fill={segColor(s)}
                  opacity={s.kind === "dwell" ? 0.55 : 0.95}
                  onMouseMove={(ev) => setHover({
                    x: ev.nativeEvent.offsetX, y: ev.nativeEvent.offsetY,
                    text: `T${r.id} · ${s.kind === "service" ? s.service : s.kind}${s.node ? " @ " + s.node : ""}${s.from ? ` ${s.from} → ${s.to}` : ""} · ${s.t0}–${s.t1}`,
                  })}
                  onMouseLeave={() => setHover(null)} />
              ))}
              {r.chips?.map((c, k) => (
                <circle key={k} cx={x(r.d_t) + 8 + k * 9} cy={y0 + rowHeight / 2} r={3.5} fill={serviceColor(inst, c)} />
              ))}
            </g>
          );
        })}
        <defs>
          <pattern id="hatch" width="6" height="6" patternUnits="userSpaceOnUse" patternTransform="rotate(45)">
            <line x1="0" y1="0" x2="0" y2="6" stroke="#ef4444" strokeOpacity="0.45" strokeWidth="2" />
          </pattern>
        </defs>
        {t !== undefined && (
          <line x1={x(t)} x2={x(t)} y1={M.t} y2={H - M.b} stroke="#f8fafc" strokeWidth={1.4} strokeDasharray="3 3" />
        )}
      </svg>
      {hover && <div className="tooltip" style={{ left: hover.x + 12, top: hover.y + 8 }}>{hover.text}</div>}
    </div>
  );
}
