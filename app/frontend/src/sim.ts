// Simulation model: where is every train at time t, what is it doing.
import type { Segment, SolvedTrain } from "./api";

export type Activity = "absent" | "moving" | "service" | "storage" | "dwell" | "rejected" | "gone";

export interface TrainState {
  train: SolvedTrain;
  activity: Activity;
  // position: a node, or an interpolation between two nodes
  node?: string;
  from?: string;
  to?: string;
  frac?: number; // 0..1 along the move
  progress?: number; // 0..1 for service / storage
  service?: string;
  segment?: Segment;
}

export function stateAt(train: SolvedTrain, t: number): TrainState {
  if (train.rejected) return { train, activity: "rejected" };
  const segs = train.segments;
  if (!segs.length) return { train, activity: "absent" };
  const arrive = segs[0];
  const depart = segs[segs.length - 1];
  if (t < arrive.t0) return { train, activity: "absent" };
  if (t > depart.t0) return { train, activity: "gone" };

  let last: Segment | undefined;
  for (const s of segs) {
    if (s.kind === "arrive" || s.kind === "depart") {
      if (s.t0 <= t) last = s;
      continue;
    }
    if (t >= s.t0 && t < s.t1) {
      if (s.kind === "move") {
        return { train, activity: "moving", from: s.from, to: s.to, frac: (t - s.t0) / (s.t1 - s.t0), segment: s };
      }
      const progress = (t - s.t0) / (s.t1 - s.t0);
      return {
        train,
        activity: s.kind === "service" ? "service" : s.kind === "storage" ? "storage" : "dwell",
        node: s.node,
        progress,
        service: s.service,
        segment: s,
      };
    }
    if (s.t1 <= t) last = s;
  }
  // instant between two segments (state transfer) or at arrival/departure
  const node = last ? (last.kind === "move" ? last.to : last.node) : arrive.node;
  return { train, activity: "dwell", node };
}

export interface SimEvent {
  t: number;
  train: number;
  text: string;
  kind: Segment["kind"];
}

/** Human-readable event list for the side panel. Consecutive moves are merged. */
export function eventLog(trains: SolvedTrain[]): SimEvent[] {
  const out: SimEvent[] = [];
  for (const tr of trains) {
    const label = `T${tr.id}`;
    if (tr.rejected) {
      out.push({ t: tr.a_t, train: tr.id, kind: "rejected", text: `${label} refused by the station` });
      continue;
    }
    let moving: Segment | null = null;
    let moveStart = "";
    const flush = () => {
      if (moving) out.push({ t: moving.t0, train: tr.id, kind: "move", text: `${label} shunts ${moveStart} → ${moving.to}` });
      moving = null;
    };
    for (const s of tr.segments) {
      if (s.kind === "move") {
        if (!moving) {
          moveStart = s.from ?? "";
          moving = { ...s };
        } else {
          moving.to = s.to;
          moving.t1 = s.t1;
        }
        continue;
      }
      flush();
      if (s.kind === "arrive") out.push({ t: s.t0, train: tr.id, kind: s.kind, text: `${label} arrives at ${s.node}` });
      else if (s.kind === "depart") out.push({ t: s.t0, train: tr.id, kind: s.kind, text: `${label} departs from ${s.node}` });
      else if (s.kind === "service")
        out.push({ t: s.t0, train: tr.id, kind: s.kind, text: `${label} starts ${s.service} at ${s.node} (${s.t1 - s.t0} min)` });
      else if (s.kind === "storage")
        out.push({ t: s.t0, train: tr.id, kind: s.kind, text: `${label} parks / turns at ${s.node}` });
    }
    flush();
  }
  return out.sort((a, b) => a.t - b.t || a.train - b.train);
}

export interface Kpis {
  present: number;
  moving: number;
  inService: number;
  parked: number;
  servicesDone: number;
  servicesRequired: number;
  costSoFar: number;
}

export function kpisAt(states: TrainState[], trains: SolvedTrain[], t: number): Kpis {
  let present = 0, moving = 0, inService = 0, parked = 0;
  for (const s of states) {
    if (["moving", "service", "storage", "dwell"].includes(s.activity)) present++;
    if (s.activity === "moving") moving++;
    if (s.activity === "service") inService++;
    if (s.activity === "storage" || s.activity === "dwell") parked++;
  }
  let servicesDone = 0, servicesRequired = 0, costSoFar = 0;
  for (const tr of trains) {
    servicesRequired += tr.required_services.length;
    if (tr.rejected) {
      if (t >= tr.a_t) costSoFar += tr.cost * Math.min(1, (t - tr.a_t) / Math.max(1, tr.d_t - tr.a_t));
      continue;
    }
    for (const s of tr.segments) if (s.kind === "service" && s.t1 <= t) servicesDone++;
    // cost accrues linearly over the train's stay
    if (t >= tr.a_t) costSoFar += tr.cost * Math.min(1, (t - tr.a_t) / Math.max(1, tr.d_t - tr.a_t));
  }
  return { present, moving, inService, parked, servicesDone, servicesRequired, costSoFar };
}
