import * as d3 from "d3";
import { useEffect, useMemo, useRef, useState } from "react";
import type { Instance } from "../api";
import { nodeCategory, nodeTypeColor, serviceColor, trainColor, trainTypeColor } from "../colors";
import type { TrainState } from "../sim";

type XY = { x: number; y: number };

interface Props {
  inst: Instance;
  states?: TrainState[];
  selected?: number | null;
  onSelect?: (id: number | null) => void;
  editable?: boolean;
  onLayoutChange?: (layout: Record<string, XY>) => void;
  colorBy?: "train" | "type";
  height?: number;
  alerts?: { from: string; to: string }[];   // links drawn in red (e.g. head-on crossings now)
}

const PAD = 60;

export default function StationMap({
  inst, states = [], selected = null, onSelect, editable = false, onLayoutChange, colorBy = "train", height = 470, alerts = [],
}: Props) {
  const svgRef = useRef<SVGSVGElement>(null);
  const gRef = useRef<SVGGElement>(null);
  const [layout, setLayout] = useState<Record<string, XY>>(inst.layout);
  const [zoom, setZoom] = useState<d3.ZoomTransform>(d3.zoomIdentity);
  const [hover, setHover] = useState<{ x: number; y: number; html: string } | null>(null);
  const drag = useRef<string | null>(null);

  useEffect(() => setLayout(inst.layout), [inst]);

  const nodeTypeOf = useMemo(() => {
    const m = new Map<string, Instance["node_types"][number]>();
    for (const n of inst.nodes) m.set(n.name, inst.node_types[n.type]);
    return m;
  }, [inst]);

  const extent = useMemo(() => {
    const xs = Object.values(layout).map((p) => p.x);
    const ys = Object.values(layout).map((p) => p.y);
    const x0 = Math.min(...xs) - PAD, x1 = Math.max(...xs) + PAD;
    const y0 = Math.min(...ys) - PAD, y1 = Math.max(...ys) + PAD;
    return { x0, y0, w: Math.max(200, x1 - x0), h: Math.max(160, y1 - y0) };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [inst]); // extent fixed per instance so dragging does not rescale the view

  // pan & zoom
  useEffect(() => {
    if (!svgRef.current) return;
    const sel = d3.select(svgRef.current);
    const z = d3
      .zoom<SVGSVGElement, unknown>()
      .scaleExtent([0.4, 6])
      .filter((ev) => !drag.current && (ev.type === "wheel" || !ev.button))
      .on("zoom", (ev) => setZoom(ev.transform));
    sel.call(z);
    sel.on("dblclick.zoom", null);
    return () => {
      sel.on(".zoom", null);
    };
  }, []);

  const toLocal = (ev: React.PointerEvent): XY | null => {
    const g = gRef.current;
    if (!g) return null;
    const ctm = g.getScreenCTM();
    if (!ctm) return null;
    const p = new DOMPoint(ev.clientX, ev.clientY).matrixTransform(ctm.inverse());
    return { x: p.x, y: p.y };
  };

  const links = inst.links.map((l) => {
    const a = inst.nodes[l.origin].name, b = inst.nodes[l.destination].name;
    const siding = a.endsWith("-1") && b.endsWith("-2") && a.slice(0, -2) === b.slice(0, -2);
    return { a, b, siding, id: l.id };
  });

  // Occupied nodes (train standing there)
  const occupied = new Map<string, TrainState>();
  for (const s of states) if (s.node && s.activity !== "moving" && (s.train.flow ?? 1) > 0.5) occupied.set(s.node, s);

  // Train token positions (+ small offsets when several tokens overlap)
  const tokens = useMemo(() => {
    const out: { s: TrainState; x: number; y: number; key: number }[] = [];
    for (const [key, s] of states.entries()) {
      if (!["moving", "service", "storage", "dwell"].includes(s.activity)) continue;
      let p: XY | undefined;
      if (s.activity === "moving" && s.from && s.to) {
        const a = layout[s.from], b = layout[s.to];
        if (a && b) p = { x: a.x + (b.x - a.x) * (s.frac ?? 0), y: a.y + (b.y - a.y) * (s.frac ?? 0) };
      } else if (s.node) p = layout[s.node];
      if (p) out.push({ s, x: p.x, y: p.y, key });
    }
    const seen = new Map<string, number>();
    for (const t of out) {
      const key = `${Math.round(t.x / 8)}:${Math.round(t.y / 8)}`;
      const k = seen.get(key) ?? 0;
      seen.set(key, k + 1);
      if (k) t.y += k * 15 * (k % 2 ? -1 : 1);
    }
    return out;
  }, [states, layout]);

  const legend = useMemo(() => {
    const seen = new Map<string, string>();
    for (const nt of inst.node_types) if (nt.nodes.length) seen.set(nt.name, nodeTypeColor(inst, nt));
    return [...seen.entries()];
  }, [inst]);

  const tokenColor = (s: TrainState) => (colorBy === "type" ? trainTypeColor(s.train.type_id) : trainColor(s.train.id));

  return (
    <div className="svg-wrap" style={{ height }}>
      <svg
        ref={svgRef}
        viewBox={`${extent.x0} ${extent.y0} ${extent.w} ${extent.h}`}
        style={{ height, cursor: editable ? "default" : "grab" }}
        onClick={() => onSelect?.(null)}
        onPointerMove={(ev) => {
          if (!drag.current) return;
          const p = toLocal(ev);
          if (p) setLayout((l) => ({ ...l, [drag.current!]: { x: Math.round(p.x), y: Math.round(p.y) } }));
        }}
        onPointerUp={() => {
          if (drag.current) {
            drag.current = null;
            onLayoutChange?.(layout);
          }
        }}
      >
        <defs>
          <filter id="glow" x="-50%" y="-50%" width="200%" height="200%">
            <feGaussianBlur stdDeviation="2.5" result="b" />
            <feMerge><feMergeNode in="b" /><feMergeNode in="SourceGraphic" /></feMerge>
          </filter>
        </defs>
        <g ref={gRef} transform={zoom.toString()}>
          {/* tracks */}
          {links.map((l) => {
            const a = layout[l.a], b = layout[l.b];
            if (!a || !b) return null;
            return (
              <line key={l.id} x1={a.x} y1={a.y} x2={b.x} y2={b.y}
                stroke={l.siding ? "#3b5b8f" : "#2a3b5c"} strokeWidth={l.siding ? 7 : 2.2} strokeLinecap="round" />
            );
          })}
          {/* alerts: links used by two trains at the same time */}
          {alerts.map((al, i) => {
            const a = layout[al.from], b = layout[al.to];
            if (!a || !b) return null;
            return (
              <g key={`al${i}`}>
                <line x1={a.x} y1={a.y} x2={b.x} y2={b.y} stroke="#ef4444" strokeWidth={12} strokeOpacity={0.35} strokeLinecap="round" />
                <line x1={a.x} y1={a.y} x2={b.x} y2={b.y} stroke="#ef4444" strokeWidth={3} strokeLinecap="round" />
                <text x={(a.x + b.x) / 2} y={(a.y + b.y) / 2 - 12} textAnchor="middle" fontSize={13} fontWeight={800} fill="#fca5a5">⚠ crossing</text>
              </g>
            );
          })}
          {/* nodes */}
          {inst.nodes.map((n) => {
            const p = layout[n.name];
            if (!p) return null;
            const nt = nodeTypeOf.get(n.name)!;
            const cat = nodeCategory(nt);
            const color = nodeTypeColor(inst, nt);
            const occ = occupied.get(n.name);
            const ring = occ ? tokenColor(occ) : "#0b1324";
            const common = {
              onPointerDown: (ev: React.PointerEvent) => {
                if (!editable) return;
                ev.stopPropagation();
                drag.current = n.name;
                (ev.target as Element).setPointerCapture?.(ev.pointerId);
              },
              onMouseEnter: (ev: React.MouseEvent) => {
                const svc = nt.services.map((b, i) => (b ? inst.services[i]?.name : null)).filter(Boolean).join(", ");
                setHover({
                  x: ev.nativeEvent.offsetX, y: ev.nativeEvent.offsetY,
                  html: `<b>${n.name}</b> · ${nt.name.trim()}<br/>${svc ? "services: " + svc : nt.io ? "entry / exit" : nt.dwelling ? "storage" : "track (no dwelling)"}${occ ? `<br/>occupied by <b>T${occ.train.id}</b>` : ""}`,
                });
              },
              onMouseLeave: () => setHover(null),
              style: { cursor: editable ? "move" : "default" },
            };
            return (
              <g key={n.id} transform={`translate(${p.x},${p.y})`} {...common}>
                {cat === "io" && <path d="M-11,0 L0,-11 L11,0 L0,11 Z" fill={color} stroke={ring} strokeWidth={2} />}
                {cat === "track" && <circle r={6.5} fill={color} stroke={occ ? ring : "#94a3b8"} strokeWidth={occ ? 3 : 1} />}
                {(cat === "storage" || cat === "service") && (
                  <rect x={-12} y={-9} width={24} height={18} rx={4} fill={color} fillOpacity={0.85}
                    stroke={ring} strokeWidth={occ ? 3 : 1.5} />
                )}
                {cat === "service" && nt.services.filter(Boolean).length > 1 && (
                  <circle cx={9} cy={-7} r={3.5} fill="#fde68a" stroke="#0b1324" />
                )}
                <text y={cat === "track" ? 19 : 23} textAnchor="middle" fontSize={10.5} fill="#9fb0cc" style={{ pointerEvents: "none" }}>
                  {n.name}
                </text>
              </g>
            );
          })}
          {/* trains */}
          {tokens.map(({ s, x, y, key }) => {
            const c = tokenColor(s);
            const sel = selected === s.train.id;
            const dim = selected !== null && !sel;
            const flow = s.train.flow ?? 1;
            const label = flow < 0.999 ? `T${s.train.id}·${flow.toFixed(2)}` : `T${s.train.id}`;
            const w = Math.max(28, label.length * 7 + 8);
            return (
              <g key={key} transform={`translate(${x},${y})`}
                opacity={(dim ? 0.3 : 1) * (0.3 + 0.7 * flow)}
                style={{ cursor: "pointer" }}
                onClick={(ev) => { ev.stopPropagation(); onSelect?.(sel ? null : s.train.id); }}
                onMouseEnter={(ev) => setHover({
                  x: ev.nativeEvent.offsetX, y: ev.nativeEvent.offsetY,
                  html: `<b>T${s.train.id}</b> (${s.train.name}) · ${s.train.type}<br/>${describe(s)}<br/>services: ${s.train.required_services.join(", ") || "none"}`,
                })}
                onMouseLeave={() => setHover(null)}
              >
                {s.activity === "service" && (
                  <>
                    <circle r={17} fill="none" stroke="#1e293b" strokeWidth={4} />
                    <circle r={17} fill="none" stroke={serviceColor(inst, s.service)} strokeWidth={4}
                      strokeDasharray={`${(s.progress ?? 0) * 2 * Math.PI * 17} ${2 * Math.PI * 17}`}
                      transform="rotate(-90)" strokeLinecap="round" />
                  </>
                )}
                <rect x={-w / 2} y={-8} width={w} height={16} rx={5} fill={c}
                  stroke={sel ? "white" : "rgba(0,0,0,0.5)"} strokeWidth={sel ? 2 : 1}
                  filter={s.activity === "moving" ? "url(#glow)" : undefined} />
                <text textAnchor="middle" y={4} fontSize={10.5} fontWeight={700} fill="#0b1120">{label}</text>
                {s.activity === "storage" && (
                  <g transform={`translate(${w / 2 + 2},-11)`}>
                    <circle r={6} fill="#3b82f6" />
                    <text textAnchor="middle" y={3.5} fontSize={8} fontWeight={800} fill="white">P</text>
                  </g>
                )}
              </g>
            );
          })}
        </g>
      </svg>
      {hover && <div className="tooltip" style={{ left: hover.x + 14, top: hover.y + 10 }} dangerouslySetInnerHTML={{ __html: hover.html }} />}
      <div className="legend" style={{ position: "absolute", left: 10, bottom: 8 }}>
        {legend.map(([name, c]) => (
          <span key={name}><span className="sw" style={{ background: c }} />{name.trim()}</span>
        ))}
      </div>
      <div className="muted small" style={{ position: "absolute", right: 10, top: 8 }}>
        {editable ? "drag nodes to edit the layout" : "scroll to zoom · drag to pan"}
      </div>
    </div>
  );
}

function describe(s: TrainState): string {
  switch (s.activity) {
    case "moving": return `shunting ${s.from} → ${s.to}`;
    case "service": return `${s.service} at ${s.node} — ${Math.round((s.progress ?? 0) * 100)} %`;
    case "storage": return `parked / turning at ${s.node}`;
    case "dwell": return `waiting at ${s.node}`;
    default: return s.activity;
  }
}
