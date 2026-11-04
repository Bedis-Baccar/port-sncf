"""Automatic schematic layout of a station (the CSVs carry no coordinates).

x = longest-path layer from the in/out nodes (links are directed in the input
files and form a DAG in every provided station), y = barycenter ordering so a
siding `pX-1 -> pX-2` ends up on one horizontal line. A user-edited layout
(`instances/<station>/layout.json`, written by the app) takes precedence.
"""
from __future__ import annotations

import json
import re
from collections import defaultdict, deque
from pathlib import Path

from .instance import Instance

X_STEP = 120.0
Y_STEP = 46.0


def _natural_key(name: str):
    return [int(t) if t.isdigit() else t for t in re.split(r"(\d+)", name)]


def _layers(inst: Instance) -> dict[int, int]:
    n = len(inst.pnodes)
    succ = defaultdict(list)
    indeg = [0] * n
    for l in inst.plinks:
        succ[l.origin].append(l.destination)
        indeg[l.destination] += 1

    # Longest path layering (Kahn); fall back to undirected BFS if there is a cycle.
    layer = [0] * n
    q = deque(i for i in range(n) if indeg[i] == 0)
    seen = 0
    indeg2 = indeg[:]
    while q:
        u = q.popleft()
        seen += 1
        for v in succ[u]:
            layer[v] = max(layer[v], layer[u] + 1)
            indeg2[v] -= 1
            if indeg2[v] == 0:
                q.append(v)
    if seen == n:
        return dict(enumerate(layer))

    io_nodes = [p for nt in inst.node_types if nt.io for p in nt.nodes] or [0]
    adj = defaultdict(set)
    for l in inst.plinks:
        adj[l.origin].add(l.destination)
        adj[l.destination].add(l.origin)
    dist = {p: 0 for p in io_nodes}
    q = deque(io_nodes)
    while q:
        u = q.popleft()
        for v in adj[u]:
            if v not in dist:
                dist[v] = dist[u] + 1
                q.append(v)
    return {i: dist.get(i, 0) for i in range(n)}


def auto_layout(inst: Instance) -> dict[str, dict[str, float]]:
    layer = _layers(inst)
    preds = defaultdict(list)
    for l in inst.plinks:
        preds[l.destination].append(l.origin)

    by_layer: dict[int, list[int]] = defaultdict(list)
    for p, k in layer.items():
        by_layer[k].append(p)

    y: dict[int, float] = {}
    for k in sorted(by_layer):
        nodes = by_layer[k]
        names = {p: inst.pnodes[p].name for p in nodes}
        bary = {}
        for p in nodes:
            ys = [y[q] for q in preds[p] if q in y]
            bary[p] = sum(ys) / len(ys) if ys else None
        if all(b is None for b in bary.values()):
            order = sorted(nodes, key=lambda p: _natural_key(names[p]))
            mid = (len(order) - 1) / 2
            for i, p in enumerate(order):
                y[p] = (i - mid) * Y_STEP
            continue
        order = sorted(nodes, key=lambda p: (bary[p] if bary[p] is not None else 0.0, _natural_key(names[p])))
        # Place at barycenter, then push apart to keep a minimum spacing.
        pos = [bary[p] if bary[p] is not None else 0.0 for p in order]
        for i in range(1, len(pos)):
            pos[i] = max(pos[i], pos[i - 1] + Y_STEP)
        # Re-center the block on the mean barycenter to stay symmetric.
        want = [b for b in (bary[p] for p in order) if b is not None]
        shift = (sum(want) / len(want)) - (sum(pos) / len(pos)) if want else 0.0
        if len(set(bary.values())) > 1 or len(order) > 1:
            pos = [v + shift for v in pos]
        for p, v in zip(order, pos):
            y[p] = v

    min_y = min(y.values(), default=0.0)
    return {
        inst.pnodes[p].name: {"x": round(layer[p] * X_STEP, 1), "y": round(y[p] - min_y, 1)}
        for p in range(len(inst.pnodes))
    }


def layout_file(station_dir: Path) -> Path:
    return Path(station_dir) / "layout.json"


def load_layout(inst: Instance, station_dir: Path) -> tuple[dict, bool]:
    """Return (positions, is_custom)."""
    auto = auto_layout(inst)
    f = layout_file(station_dir)
    if f.exists():
        try:
            saved = json.loads(f.read_text(encoding="utf-8"))
            merged = {name: saved.get(name, pos) for name, pos in auto.items()}
            return merged, True
        except (json.JSONDecodeError, OSError):
            pass
    return auto, False
