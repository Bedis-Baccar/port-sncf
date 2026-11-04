"""Decode solver paths (arc id sequences) into human-readable segments,
validate them, and check the solution for node/time conflicts."""
from __future__ import annotations

from collections import defaultdict

from .graph import (
    ARRIVAL, DEPARTURE, DUMMY, DWELLING, SERVICE, SHUNTING, STATE_TRANSFER, STORAGE,
    TimeSpaceGraph,
)

COST_BUCKET = {
    SHUNTING: "shunting", SERVICE: "service", STORAGE: "storage",
    DWELLING: "dwelling", DUMMY: "rejection",
}


class PathError(ValueError):
    pass


def decode_path(g: TimeSpaceGraph, train_id: int, arc_ids: list[int]) -> dict:
    """Return {segments, cost, cost_breakdown, services_done, rejected, occupancy}.
    Raises PathError if the arc sequence is not a valid s->t path for the train."""
    inst = g.inst
    train = inst.trains[train_id]
    if not arc_ids:
        raise PathError("empty path")
    try:
        arcs = [g.arcs[a] for a in arc_ids]
    except IndexError as e:
        raise PathError(f"arc id out of range ({len(g.arcs)} arcs)") from e

    if arcs[0].frm != 0 or arcs[-1].to != 1:
        raise PathError("path does not go from source to sink")
    for a, b in zip(arcs, arcs[1:]):
        if a.to != b.frm:
            raise PathError("path is not contiguous")

    name = lambda p: inst.pnodes[p].name  # noqa: E731
    segments: list[dict] = []
    breakdown: dict[str, int] = defaultdict(int)
    services_done: list[int] = []
    occupancy: list[tuple[int, int]] = []
    cost = 0

    for a in arcs:
        c = g.arc_cost(a, train_id)
        cost += c
        if a.type in COST_BUCKET:
            breakdown[COST_BUCKET[a.type]] += c
        occupancy.extend(a.inodes)
        p0, _, _ = g.decode_vnode(a.frm)
        p1, _, _ = g.decode_vnode(a.to)

        if a.type in (ARRIVAL, DEPARTURE, DUMMY) and a.train != train_id:
            raise PathError(f"arc belongs to train {a.train}")
        if a.type == STORAGE and a.train != train.type:
            raise PathError("storage arc of another train type")
        if a.type == SERVICE and not train.services[a.service]:
            raise PathError("service not required by train")

        if a.type == ARRIVAL:
            segments.append({"kind": "arrive", "node": name(p1), "t0": a.t1, "t1": a.t1})
        elif a.type == DEPARTURE:
            segments.append({"kind": "depart", "node": name(p0), "t0": a.t0, "t1": a.t0})
        elif a.type == SHUNTING:
            segments.append({"kind": "move", "from": name(p0), "to": name(p1), "t0": a.t0, "t1": a.t1})
        elif a.type == SERVICE:
            services_done.append(a.service)
            segments.append({"kind": "service", "node": name(p0), "service": inst.services[a.service].name,
                             "t0": a.t0, "t1": a.t1})
        elif a.type == STORAGE:
            segments.append({"kind": "storage", "node": name(p0), "t0": a.t0, "t1": a.t1})
        elif a.type == DWELLING:
            last = segments[-1] if segments else None
            if last and last["kind"] == "dwell" and last["node"] == name(p0) and last["t1"] == a.t0:
                last["t1"] = a.t1
            else:
                segments.append({"kind": "dwell", "node": name(p0), "t0": a.t0, "t1": a.t1})
        elif a.type == STATE_TRANSFER:
            pass  # instantaneous layer change, not visible physically
        elif a.type == DUMMY:
            segments.append({"kind": "rejected", "t0": train.a_t, "t1": train.d_t})

    return {
        "segments": segments,
        "cost": cost,
        "cost_breakdown": dict(breakdown),
        "services_done": [inst.services[s].name for s in services_done],
        "rejected": arcs[0].type == DUMMY,
        "occupancy": occupancy,
    }


def decode_solution(g: TimeSpaceGraph, train_paths: list[dict]) -> dict:
    """train_paths: [{train_id, cost, arc_sequence}] as written by the solver."""
    inst = g.inst
    trains = []
    occ: dict[tuple[int, int], list[int]] = defaultdict(list)
    for tp in sorted(train_paths, key=lambda x: x["train_id"]):
        k = tp["train_id"]
        d = decode_path(g, k, tp["arc_sequence"])
        if "cost" in tp and d["cost"] != tp["cost"]:
            raise PathError(f"train {k}: cost {d['cost']} != reported {tp['cost']}")
        for pt in d.pop("occupancy"):
            occ[pt].append(k)
        tr = inst.trains[k]
        trains.append({
            "id": k,
            "name": tr.name,
            "type": inst.train_types[tr.type].name,
            "type_id": tr.type,
            "a_t": tr.a_t,
            "d_t": tr.d_t,
            "required_services": [inst.services[s].name for s in tr.required_services],
            **d,
        })

    # Two kinds of violations, both checked independently of the model that produced
    # the solution: a node used by two trains (original constraints) and a link used
    # by two trains in the same step (head-on crossing, forbidden by link conflicts).
    conflicts = []
    for (r, t), ks in sorted(occ.items()):
        if len(set(ks)) < 2:
            continue
        if g.is_link_resource(r):
            a, b = g.resource_names[r].split("|")
            conflicts.append({"kind": "link", "node": f"{a}–{b}", "from": a, "to": b, "t": t, "trains": ks})
        else:
            conflicts.append({"kind": "node", "node": inst.pnodes[r].name, "t": t, "trains": ks})
    return {"trains": trains, "conflicts": conflicts}


VARIANTS = [
    {"legacy_state_transfer": False, "io_transfer_quirk": False},  # fixed solver
    {"legacy_state_transfer": True, "io_transfer_quirk": False},   # old solver
    {"legacy_state_transfer": True, "io_transfer_quirk": True},    # old solver + UB bit set
]
