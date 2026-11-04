import { useEffect, useRef, useState } from "react";

/** Tracks the width of an element with a ResizeObserver. */
export function useWidth<T extends HTMLElement>(initial = 1000) {
  const ref = useRef<T>(null);
  const [width, setWidth] = useState(initial);
  useEffect(() => {
    if (!ref.current) return;
    const ro = new ResizeObserver(([e]) => setWidth(Math.max(320, Math.floor(e.contentRect.width))));
    ro.observe(ref.current);
    return () => ro.disconnect();
  }, []);
  return [ref, width] as const;
}
