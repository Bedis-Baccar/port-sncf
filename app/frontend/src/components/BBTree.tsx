import * as d3 from "d3";
import { useEffect, useMemo, useRef, useState } from "react";
import type { TreeNode } from "../trace";
import { STATUS_COLOR, nodeStatusAt } from "../trace";

interface Props {
  nodes: Map<number, TreeNode>;
  t: number;
  current?: number | null;
  selected?: number | null;
  onSelect?: (id: number | null) => void;
  height?: number;
}

/** Branch-and-price search tree as it was at solver time t. */
export default function BBTree({ nodes, t, current = null, selected = null, onSelect, height = 460 }: Props) {
  const svgRef = useRef<SVGSVGElement>(null);
  const [zoom, setZoom] = useState(d3.zoomIdentity);
  const [hover, setHover] = useState<{ x: number; y: number; html: string } | null>(null);

  const layout = useMemo(() => {
    const visible = [...nodes.values()].filter((n) => n.created <= t);
    if (!visible.length || !visible.some((n) => n.id === 0)) return null;
    const ids = new Set(visible.map((n) => n.id));
    const strat = d3
      .stratify<TreeNode>()
      .id((d) => String(d.id))
      .parentId((d) => (d.id === 0 || !ids.has(d.parent) ? null : String(d.parent)));
    let root: d3.HierarchyNode<TreeNode>;
    try {
      root = strat(visible.filter((n) => n.id === 0 || ids.has(n.parent)));
    } catch {
      return null;
    }
    root.sort((a, b) => a.data.id - b.data.id);
    const tree = d3.tree<TreeNode>().nodeSize([22, 54]);
    const laid = tree(root);
    let x0 = Infinity, x1 = -Infinity, y1 = 0;
    laid.each((d) => {
      x0 = Math.min(x0, d.x);
      x1 = Math.max(x1, d.x);
      y1 = Math.max(y1, d.y);
    });
    return { root: laid, x0, x1, y1 };
  }, [nodes, t]);

  useEffect(() => {
    if (!svgRef.current) return;
    const sel = d3.select(svgRef.current);
    const z = d3.zoom<SVGSVGElement, unknown>().scaleExtent([0.1, 4]).on("zoom", (ev) => setZoom(ev.transform));
    sel.call(z);
    return () => {
      sel.on(".zoom", null);
    };
  }, []);

  if (!layout) return <div className="empty">No tree yet — the root node has not been processed at this time.</div>;
  const { root, x0, x1, y1 } = layout;
  const pad = 30;
  const vb = `${x0 - pad} ${-pad} ${Math.max(300, x1 - x0 + 2 * pad)} ${Math.max(200, y1 + 2 * pad)}`;

  return (
    <div className="svg-wrap" style={{ height }}>
      <svg ref={svgRef} viewBox={vb} style={{ height, cursor: "grab" }} preserveAspectRatio="xMidYMin meet">
        <g transform={zoom.toString()}>
          {root.links().map((l) => (
            <path key={`${l.source.data.id}-${l.target.data.id}`}
              d={`M${l.source.x},${l.source.y} C${l.source.x},${(l.source.y + l.target.y) / 2} ${l.target.x},${(l.source.y + l.target.y) / 2} ${l.target.x},${l.target.y}`}
              fill="none" stroke="#2b3d60" strokeWidth={1.3} />
          ))}
          {root.descendants().map((d) => {
            const st = nodeStatusAt(d.data, t);
            const status = st?.status ?? "UNPROCESSED";
            const isCur = current === d.data.id;
            const isSel = selected === d.data.id;
            return (
              <g key={d.data.id} transform={`translate(${d.x},${d.y})`} style={{ cursor: "pointer" }}
                onClick={(ev) => { ev.stopPropagation(); onSelect?.(isSel ? null : d.data.id); }}
                onMouseEnter={(ev) => setHover({
                  x: ev.nativeEvent.offsetX, y: ev.nativeEvent.offsetY,
                  html: `<b>node ${d.data.id}</b> · depth ${d.data.depth}<br/>${d.data.label || ""}<br/>status: <b>${status}</b><br/>LB: ${st?.lb?.toFixed(2) ?? "—"}`,
                })}
                onMouseLeave={() => setHover(null)}>
                {isCur && <circle r={11} fill="none" stroke="#f8fafc" strokeWidth={2} strokeDasharray="3 2" />}
                <circle r={7} fill={STATUS_COLOR[status] ?? "#64748b"} stroke={isSel ? "white" : "#0b1324"} strokeWidth={isSel ? 2.5 : 1.5} />
              </g>
            );
          })}
        </g>
      </svg>
      {hover && <div className="tooltip" style={{ left: hover.x + 12, top: hover.y + 8 }} dangerouslySetInnerHTML={{ __html: hover.html }} />}
      <div className="legend" style={{ position: "absolute", left: 10, bottom: 8 }}>
        {Object.entries(STATUS_COLOR).map(([k, c]) => (
          <span key={k}><span className="sw" style={{ background: c, borderRadius: "50%" }} />{k.toLowerCase()}</span>
        ))}
      </div>
    </div>
  );
}
