import { useCallback, useEffect, useRef, useState } from "react";
import { api, type Instance, type RunInfo, type Solution, type TraceEvent } from "./api";
import { buildModel, emptyModel, ingest, type TraceModel } from "./trace";

/** Loads everything about a run; streams trace events over a WebSocket while it is running. */
export function useRunData(id: string) {
  const [info, setInfo] = useState<RunInfo | null>(null);
  const [inst, setInst] = useState<Instance | null>(null);
  const [solution, setSolution] = useState<Solution | null>(null);
  const [solutionError, setSolutionError] = useState<string | null>(null);
  const model = useRef<TraceModel>(emptyModel());
  const [version, setVersion] = useState(0);
  const [log, setLog] = useState<string[]>([]);
  const [error, setError] = useState<string | null>(null);
  const [live, setLive] = useState(false);

  const loadSolution = useCallback(() => {
    api.solution(id).then((s) => { setSolution(s); setSolutionError(null); }).catch((e) => setSolutionError(e.message));
  }, [id]);

  useEffect(() => {
    let ws: WebSocket | null = null;
    let cancelled = false;
    model.current = emptyModel();
    setSolution(null);
    setLog([]);
    setVersion((v) => v + 1);

    (async () => {
      try {
        const i = await api.run(id);
        if (cancelled) return;
        setInfo(i);
        if (i.station && i.scenario) api.instance(i.station, i.scenario).then((x) => !cancelled && setInst(x)).catch((e) => setError(e.message));
        if (i.has_solution) loadSolution();

        if (i.running) {
          setLive(true);
          ws = new WebSocket(api.liveUrl(id));
          ws.onmessage = (m) => {
            const msg = JSON.parse(m.data) as { type: string; events?: TraceEvent[]; lines?: string[] };
            if (msg.type === "events" && msg.events) {
              ingest(model.current, msg.events);
              setVersion((v) => v + 1);
            } else if (msg.type === "log" && msg.lines) {
              setLog((l) => [...l, ...msg.lines!].slice(-2000));
            } else if (msg.type === "done") {
              setLive(false);
              api.run(id).then(setInfo);
              loadSolution();
            }
          };
          ws.onclose = () => setLive(false);
        } else {
          const evs = await api.events(id);
          if (cancelled) return;
          model.current = buildModel(evs);
          setVersion((v) => v + 1);
          if (i.kind === "v2") api.log(id).then((r) => !cancelled && setLog(r.lines));
        }
      } catch (e) {
        if (!cancelled) setError((e as Error).message);
      }
    })();
    return () => {
      cancelled = true;
      ws?.close();
    };
  }, [id, loadSolution]);

  return { info, setInfo, inst, solution, solutionError, model: model.current, version, log, error, live };
}
