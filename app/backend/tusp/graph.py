"""Time-space graph rebuild — mirrors src/builder/GraphBuilder.cpp arc by arc.

The solver identifies paths by arc index, so the arcs must be produced in the
exact same order as the C++ builder. This lets the app decode solution files
written by older solver versions that only contain `arc_sequence` ids.
"""
from __future__ import annotations

from dataclasses import dataclass
from functools import reduce
from math import gcd

from .instance import Instance

# src/param.h
T_H = 5
C_SHU = 2
W_DWELL = 1
C_PARK = 5
W_SHU = 2

ARRIVAL, DEPARTURE, SHUNTING, SERVICE, STORAGE, DWELLING, STATE_TRANSFER, DUMMY = range(8)
ARC_TYPE_NAMES = [
    "ARRIVAL", "DEPARTURE", "SHUNTING", "SERVICE",
    "STORAGE", "DWELLING", "STATE_TRANSFER", "DUMMY",
]


@dataclass(slots=True)
class Arc:
    type: int
    frm: int          # virtual node id
    to: int           # virtual node id
    t0: int
    t1: int
    cost: int         # base cost (train-independent part)
    service: int = -1
    train: int = -1   # ARRIVAL/DEPARTURE/DUMMY: owner train; STORAGE: train type
    inodes: tuple = ()  # ((pnode, t), ...) occupied for conflict constraints


class TimeSpaceGraph:
    def __init__(self, inst: Instance, legacy_state_transfer: bool = False, io_transfer_quirk: bool = False):
        """Variants needed to decode results of the *old* solver:
        - legacy_state_transfer: the old loop `for s=1..n_services` skipped service 0.
        - io_transfer_quirk: the old loop also read services[n_services] (out of
          bounds, undefined behaviour); when that garbage bit was 1, in/out nodes
          received state-transfer arcs too. Results differ run to run, so the
          decoder tries both and keeps the variant whose paths validate."""
        self.variant = {"legacy_state_transfer": legacy_state_transfer, "io_transfer_quirk": io_transfer_quirk}
        self.inst = inst
        self.n_p = len(inst.pnodes)
        self.ns = len(inst.services)
        self._compute_time_step()
        self.arcs: list[Arc] = []
        self.dummy_arc: dict[int, int] = {}
        self._build_ad()
        self._build_shunting()
        self._build_service()
        self._build_storage()
        self._build_dwelling()
        self._build_state_transfer(legacy_state_transfer, io_transfer_quirk)
        self._build_dummy()

    # -- helpers ---------------------------------------------------------
    def _compute_time_step(self):
        inst = self.inst
        vals = []
        for t in inst.trains:
            vals += [t.a_t, t.d_t]
        vals += [s.duration for s in inst.services]
        vals += [tt.t_turn for tt in inst.train_types]
        vals += [l.length for l in inst.plinks]
        self.t_s = reduce(gcd, vals, 0)
        self.T = max((t.d_t for t in inst.trains), default=0)

    def vid(self, p: int, t: int, l: int) -> int:
        if p == -1:
            return l - 1
        return p * 3 + (t // self.t_s) * self.n_p * 3 + l + 2

    def decode_vnode(self, v: int) -> tuple[int, int, int]:
        """virtual node id -> (pnode, time, layer); source/sink -> (-1, t, layer)."""
        if v == 0:
            return (-1, 0, 1)
        if v == 1:
            return (-1, self.T, 2)
        x = v - 2
        l = x % 3
        p = (x // 3) % self.n_p
        t = (x // (3 * self.n_p)) * self.t_s
        return (p, t, l)

    def arc_cost(self, arc: Arc, train_id: int) -> int:
        if arc.type == SHUNTING:
            tr = self.inst.trains[train_id]
            w = self.inst.train_types[tr.type].w_shu
            return w * (arc.t1 - arc.t0) + C_SHU
        return arc.cost

    # -- builders (same order as GraphBuilder::build) ---------------------
    def _build_ad(self):
        T, ts = self.T, self.t_s
        for tr in self.inst.trains:
            for nt in self.inst.node_types:
                if not nt.io:
                    continue
                for p in nt.nodes:
                    self.arcs.append(Arc(ARRIVAL, 0, self.vid(p, tr.a_t, 1), 0, tr.a_t, 0, train=tr.id))
                    inodes = tuple((p, tr.d_t + dt) for dt in range(0, min(T_H, T - tr.d_t + 1), ts))
                    self.arcs.append(
                        Arc(DEPARTURE, self.vid(p, tr.d_t, 2), 1, tr.d_t, T, 0, train=tr.id, inodes=inodes)
                    )

    def _shunt(self, p, q, l, t1, d):
        inodes = tuple((p, t1 + dt) for dt in range(0, min(T_H, self.T - t1 + 1), self.t_s))
        self.arcs.append(
            Arc(SHUNTING, self.vid(p, t1, l), self.vid(q, t1 + d, l), t1, t1 + d, W_SHU * d + C_SHU, inodes=inodes)
        )

    def _build_shunting(self):
        T, ts = self.T, self.t_s
        for link in self.inst.plinks:
            d = link.length
            for t1 in range(0, T, ts):
                if t1 + d > T:
                    break
                self._shunt(link.origin, link.destination, 1, t1, d)
                self._shunt(link.destination, link.origin, 2, t1, d)

    def _occupy(self, p, t1, d):
        return tuple((p, t1 + dt) for dt in range(0, d, self.t_s))

    def _build_service(self):
        T, ts = self.T, self.t_s
        for s in self.inst.services:
            d = s.duration
            for nt in self.inst.node_types:
                if not nt.services[s.id]:
                    continue
                for p in nt.nodes:
                    for l in (1, 2):
                        for t1 in range(0, T - d + 1, ts):
                            self.arcs.append(
                                Arc(SERVICE, self.vid(p, t1, l), self.vid(p, t1 + d, 0), t1, t1 + d,
                                    s.cost, service=s.id, inodes=self._occupy(p, t1, d))
                            )

    def _build_storage(self):
        T, ts = self.T, self.t_s
        for nt in self.inst.node_types:
            if not nt.parking:
                continue
            for tt in self.inst.train_types:
                d = tt.t_turn
                for p in nt.nodes:
                    for l in (1, 2):
                        for t1 in range(0, T - d + 1, ts):
                            self.arcs.append(
                                Arc(STORAGE, self.vid(p, t1, l), self.vid(p, t1 + d, 0), t1, t1 + d,
                                    W_DWELL * d + C_PARK, train=tt.id, inodes=self._occupy(p, t1, d))
                            )

    def _build_dwelling(self):
        T, ts = self.T, self.t_s
        for nt in self.inst.node_types:
            if not nt.dwelling:
                continue
            for p in nt.nodes:
                for t1 in range(0, T, ts):
                    self.arcs.append(
                        Arc(DWELLING, self.vid(p, t1, 0), self.vid(p, t1 + ts, 0), t1, t1 + ts,
                            W_DWELL * ts, inodes=((p, t1),))
                    )

    def _build_state_transfer(self, legacy: bool, quirk: bool):
        T, ts = self.T, self.t_s
        for nt in self.inst.node_types:
            svc = nt.services[1:] if legacy else nt.services
            if not (nt.parking or any(svc) or quirk):
                continue
            for p in nt.nodes:
                for l in (1, 2):
                    for t1 in range(0, T, ts):
                        self.arcs.append(Arc(STATE_TRANSFER, self.vid(p, t1, 0), self.vid(p, t1, l), t1, t1, 0))

    def _build_dummy(self):
        csum = 2 * sum(s.cost for s in self.inst.services)
        for tr in self.inst.trains:
            w = self.inst.train_types[tr.type].w_shu + W_DWELL + C_PARK + C_SHU
            d = tr.d_t - tr.a_t
            self.dummy_arc[tr.id] = len(self.arcs)
            self.arcs.append(Arc(DUMMY, 0, 1, 0, self.T, w * d + csum, train=tr.id))

    # -- stats ---------------------------------------------------------------
    def arc_type_counts(self) -> dict[str, int]:
        counts = [0] * len(ARC_TYPE_NAMES)
        for a in self.arcs:
            counts[a.type] += 1
        return dict(zip(ARC_TYPE_NAMES, counts))
