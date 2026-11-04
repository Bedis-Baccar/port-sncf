import { useEffect, useRef, useState } from "react";

/** Animation clock: t advances by `speed` time units per second while playing. */
export function usePlayback(tMax: number, initial = 0) {
  const [t, setT] = useState(initial);
  const [playing, setPlaying] = useState(false);
  const [speed, setSpeed] = useState(10);
  const last = useRef<number | null>(null);

  useEffect(() => {
    if (!playing) {
      last.current = null;
      return;
    }
    let raf = 0;
    const tick = (now: number) => {
      if (last.current !== null) {
        const dt = ((now - last.current) / 1000) * speed;
        setT((prev) => {
          const next = prev + dt;
          if (next >= tMax) {
            setPlaying(false);
            return tMax;
          }
          return next;
        });
      }
      last.current = now;
      raf = requestAnimationFrame(tick);
    };
    raf = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(raf);
  }, [playing, speed, tMax]);

  const toggle = () => {
    if (!playing && t >= tMax) setT(0);
    setPlaying((p) => !p);
  };
  return { t, setT, playing, setPlaying, toggle, speed, setSpeed };
}

interface Props {
  t: number;
  tMax: number;
  step: number;
  playing: boolean;
  speed: number;
  speeds: number[];
  onToggle: () => void;
  onSeek: (t: number) => void;
  onSpeed: (s: number) => void;
  format?: (t: number) => string;
  unit?: string;
  extra?: React.ReactNode;
}

export default function PlayerBar({ t, tMax, step, playing, speed, speeds, onToggle, onSeek, onSpeed, format, unit = "", extra }: Props) {
  // keyboard: space = play/pause, arrows = step
  useEffect(() => {
    const on = (e: KeyboardEvent) => {
      if ((e.target as HTMLElement)?.tagName === "INPUT" || (e.target as HTMLElement)?.tagName === "SELECT") return;
      if (e.code === "Space") { e.preventDefault(); onToggle(); }
      if (e.code === "ArrowRight") onSeek(Math.min(tMax, Math.floor(t / step) * step + step));
      if (e.code === "ArrowLeft") onSeek(Math.max(0, Math.ceil(t / step) * step - step));
    };
    window.addEventListener("keydown", on);
    return () => window.removeEventListener("keydown", on);
  }, [t, tMax, step, onToggle, onSeek]);

  return (
    <div className="player">
      <button className="icon" title="Start" onClick={() => onSeek(0)}>⏮</button>
      <button className="icon" title="Step back (←)" onClick={() => onSeek(Math.max(0, Math.ceil(t / step) * step - step))}>◀</button>
      <button className="primary icon" title="Play / pause (space)" onClick={onToggle} style={{ minWidth: 42, justifyContent: "center" }}>
        {playing ? "❚❚" : "▶"}
      </button>
      <button className="icon" title="Step forward (→)" onClick={() => onSeek(Math.min(tMax, Math.floor(t / step) * step + step))}>▶</button>
      <button className="icon" title="End" onClick={() => onSeek(tMax)}>⏭</button>
      <span className="clock">{format ? format(t) : `${t.toFixed(0)}${unit}`}</span>
      <input type="range" min={0} max={tMax} step={step / 10} value={t} onChange={(e) => onSeek(+e.target.value)} />
      <span className="muted small">{format ? format(tMax) : `${tMax}${unit}`}</span>
      <div className="seg" title="Playback speed">
        {speeds.map((s) => (
          <button key={s} className={s === speed ? "on" : ""} onClick={() => onSpeed(s)}>
            {s}×
          </button>
        ))}
      </div>
      {extra}
    </div>
  );
}
