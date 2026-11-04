import * as echarts from "echarts";
import { useEffect, useRef } from "react";

type Props = {
  option: echarts.EChartsOption;
  height?: number | string;
  onClick?: (p: echarts.ECElementEvent) => void;
  notMerge?: boolean;
};

/** Thin React wrapper around ECharts (resizes with its container). */
export default function EChart({ option, height = 280, onClick, notMerge = true }: Props) {
  const el = useRef<HTMLDivElement>(null);
  const chart = useRef<echarts.ECharts | null>(null);
  const clickRef = useRef(onClick);
  clickRef.current = onClick;

  useEffect(() => {
    if (!el.current) return;
    const c = echarts.init(el.current, "dark", { renderer: "canvas" });
    chart.current = c;
    c.on("click", (p) => clickRef.current?.(p as echarts.ECElementEvent));
    const ro = new ResizeObserver(() => c.resize());
    ro.observe(el.current);
    return () => {
      ro.disconnect();
      c.dispose();
      chart.current = null;
    };
  }, []);

  useEffect(() => {
    chart.current?.setOption({ backgroundColor: "transparent", ...option }, { notMerge, lazyUpdate: true });
  }, [option, notMerge]);

  return <div ref={el} style={{ width: "100%", height }} />;
}
