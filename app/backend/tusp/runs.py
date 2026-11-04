"""Discovery and reading of solver runs.

Two layouts are supported:
- v2 (current solver):  results/runs/<run_id>/{run.json, solution.json, trace.jsonl, dynamics.csv}
- legacy (old solver):  results/solutions/solution_<name>.json + results/dynamics/dynamics_<name>.csv
                        + a row in results/summary_benchmark.csv
"""
from __future__ import annotations

import csv
import json
import re
import threading
from functools import lru_cache
from pathlib import Path

from .graph import TimeSpaceGraph
from .instance import Instance, load_instance
from .solution import VARIANTS, PathError, decode_path, decode_solution

NAME_RE = re.compile(r"^(?P<station>.+?)_sce_(?P<k>\d+)")


class Paths:
    def __init__(self, root: Path):
        self.root = Path(root)
        self.instances = self.root / "instances"
        self.results = self.root / "results"
        self.runs = self.results / "runs"
        self.legacy_solutions = self.results / "solutions"
        self.legacy_dynamics = self.results / "dynamics"
        self.summary = self.results / "summary_benchmark.csv"


def guess_instance(name: str) -> tuple[str, str] | None:
    """'small_station_sce_1[_suffix]' -> ('small station', 'scenario 1')."""
    m = NAME_RE.match(name)
    if not m:
        return None
    return m.group("station").replace("_", " "), f"scenario {m.group('k')}"


# ---------------------------------------------------------------- caches ----
_lock = threading.Lock()


@lru_cache(maxsize=64)
def cached_instance(scenario_dir: str) -> Instance:
    return load_instance(Path(scenario_dir))


@lru_cache(maxsize=6)
def cached_graph(scenario_dir: str, variant_idx: int) -> TimeSpaceGraph:
    return TimeSpaceGraph(cached_instance(scenario_dir), **VARIANTS[variant_idx])


def _read_json(path: Path):
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return None


def _f(x):
    try:
        return float(x)
    except (TypeError, ValueError):
        return None


def read_summary(paths: Paths) -> dict[str, dict]:
    if not paths.summary.exists():
        return {}
    with open(paths.summary, encoding="utf-8-sig", newline="") as f:
        return {r["Instance"]: r for r in csv.DictReader(f)}


def read_events(trace: Path, strip_paths: bool = True) -> list[dict]:
    events = []
    if not trace.exists():
        return events
    inc = 0
    with open(trace, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                ev = json.loads(line)
            except json.JSONDecodeError:
                continue  # partially written last line of a live run
            if ev.get("ev") == "incumbent":
                ev["idx"] = inc
                inc += 1
            if strip_paths and "train_paths" in ev:
                ev["n_paths"] = len(ev.pop("train_paths"))
            events.append(ev)
    return events


def strip_event(ev: dict, idx_counter: list[int]) -> dict:
    ev = dict(ev)
    if ev.get("ev") == "incumbent":
        ev["idx"] = idx_counter[0]
        idx_counter[0] += 1
    if "train_paths" in ev:
        ev["n_paths"] = len(ev.pop("train_paths"))
    return ev


def legacy_events(dyn: Path, summary_row: dict | None) -> list[dict]:
    """Synthesize trace events from the old dynamics CSV."""
    events: list[dict] = []
    if dyn.exists():
        with open(dyn, encoding="utf-8-sig", newline="") as f:
            for r in csv.DictReader(f):
                events.append({
                    "ev": "bounds", "t": _f(r.get("Time_s")), "it": int(float(r.get("Nodes") or 0)),
                    "pool": int(float(r.get("Columns") or 0)), "lb": _f(r.get("LB")), "ub": _f(r.get("UB")),
                    "gap": _f(r.get("Gap")) if r.get("UB") else None,
                })
    if summary_row:
        events.append({
            "ev": "end", "t": _f(summary_row.get("TotalTime_s")),
            "status": "OPTIMAL" if (_f(summary_row.get("Gap_%")) or 0) < 1e-6 else "TIME_LIMIT",
            "root_lb": _f(summary_row.get("Root_LB")), "lb": _f(summary_row.get("Final_LB")),
            "ub": _f(summary_row.get("Final_UB")), "gap": _f(summary_row.get("Gap_%")),
            "ttfs": _f(summary_row.get("TTFS_s")), "nodes": int(float(summary_row.get("Nodes") or 0)),
            "t_master": _f(summary_row.get("MasterTime_s")), "t_pricing": _f(summary_row.get("PricingTime_s")),
            "t_heur": _f(summary_row.get("HeuristicsTime_s")),
        })
    return events


class RunStore:
    def __init__(self, paths: Paths):
        self.paths = paths

    # ------------------------------------------------------------ listing --
    def scenario_dir(self, station: str, scenario: str) -> Path:
        d = (self.paths.instances / station / scenario).resolve()
        if self.paths.instances.resolve() not in d.parents or not d.is_dir():
            raise FileNotFoundError(f"unknown instance {station}/{scenario}")
        return d

    def list_runs(self) -> list[dict]:
        runs = []
        summary = read_summary(self.paths)
        if self.paths.runs.exists():
            for d in sorted(self.paths.runs.iterdir()):
                if d.is_dir():
                    info = self._v2_info(d)
                    if info:
                        runs.append(info)
        if self.paths.legacy_solutions.exists():
            for f in sorted(self.paths.legacy_solutions.glob("solution_*.json")):
                name = f.stem[len("solution_"):]
                runs.append(self._legacy_info(name, summary.get(name)))
        return runs

    def _v2_info(self, d: Path) -> dict | None:
        meta = _read_json(d / "run.json") or {}
        sol = _read_json(d / "solution.json") or {}
        end = None
        trace = d / "trace.jsonl"
        if not meta and not sol and not trace.exists():
            return None
        if trace.exists():
            # only the last line is needed for the summary
            with open(trace, "rb") as f:
                f.seek(0, 2)
                size = f.tell()
                f.seek(max(0, size - 4096))
                tail = f.read().decode("utf-8", "ignore").strip().splitlines()
            for line in reversed(tail):
                try:
                    ev = json.loads(line)
                except json.JSONDecodeError:
                    continue
                if ev.get("ev") == "end":
                    end = ev
                break
        inst = (meta.get("station"), meta.get("scenario")) if meta.get("station") else guess_instance(d.name)
        stats = sol.get("stats", {})
        params = meta.get("params") or {}
        method = params.get("method") or sol.get("method") or (sol.get("stats") or {}).get("method") or "bp"
        link = params.get("link_conflicts", (sol.get("graph") or {}).get("link_conflicts", False))
        return {
            "id": d.name,
            "kind": "v2",
            "method": method,
            "link_conflicts": bool(link),
            "batch": meta.get("batch"),
            "station": inst[0] if inst else None,
            "scenario": inst[1] if inst else None,
            "created": meta.get("created"),
            "params": meta.get("params"),
            "state": meta.get("state", "finished" if sol else "unknown"),
            "status": (end or {}).get("status") or sol.get("status"),
            "objective": sol.get("objective_value"),
            "lb": (end or {}).get("lb", stats.get("final_lb")),
            "gap": (end or {}).get("gap", stats.get("gap_pct")),
            "time_s": (end or {}).get("t", stats.get("total_time_s")),
            "ttfs": (end or {}).get("ttfs", stats.get("ttfs_s")),
            "has_solution": bool(sol.get("train_paths")),
            "has_trace": trace.exists(),
        }

    def _legacy_info(self, name: str, row: dict | None) -> dict:
        inst = guess_instance(name)
        sol = _read_json(self.paths.legacy_solutions / f"solution_{name}.json") or {}
        return {
            "id": f"legacy:{name}",
            "kind": "legacy",
            "method": "bp",
            "link_conflicts": False,
            "batch": None,
            "station": inst[0] if inst else None,
            "scenario": inst[1] if inst else None,
            "created": None,
            "params": None,
            "state": "finished",
            "status": None if not row else ("OPTIMAL" if (_f(row.get("Gap_%")) or 0) < 1e-6 else "TIME_LIMIT"),
            "objective": sol.get("objective_value"),
            "lb": _f(row.get("Final_LB")) if row else None,
            "gap": _f(row.get("Gap_%")) if row else None,
            "time_s": _f(row.get("TotalTime_s")) if row else None,
            "ttfs": _f(row.get("TTFS_s")) if row else None,
            "has_solution": bool(sol.get("train_paths")),
            "has_trace": False,
        }

    # ------------------------------------------------------------ reading --
    def run_dir(self, run_id: str) -> Path:
        if run_id.startswith("legacy:") or "/" in run_id or ".." in run_id:
            raise FileNotFoundError(run_id)
        d = self.paths.runs / run_id
        if not d.is_dir():
            raise FileNotFoundError(run_id)
        return d

    def info(self, run_id: str) -> dict:
        if run_id.startswith("legacy:"):
            name = run_id.split(":", 1)[1]
            if not (self.paths.legacy_solutions / f"solution_{name}.json").exists():
                raise FileNotFoundError(run_id)
            return self._legacy_info(name, read_summary(self.paths).get(name))
        info = self._v2_info(self.run_dir(run_id))
        if not info:
            raise FileNotFoundError(run_id)
        return info

    def events(self, run_id: str) -> list[dict]:
        if run_id.startswith("legacy:"):
            name = run_id.split(":", 1)[1]
            return legacy_events(self.paths.legacy_dynamics / f"dynamics_{name}.csv",
                                 read_summary(self.paths).get(name))
        return read_events(self.run_dir(run_id) / "trace.jsonl")

    def _raw_solution(self, run_id: str) -> dict:
        if run_id.startswith("legacy:"):
            name = run_id.split(":", 1)[1]
            sol = _read_json(self.paths.legacy_solutions / f"solution_{name}.json")
        else:
            sol = _read_json(self.run_dir(run_id) / "solution.json")
        if sol is None:
            raise FileNotFoundError("no solution file")
        return sol

    def _graph_for(self, run_id: str, train_paths: list[dict], num_arcs: int | None = None):
        info = self.info(run_id)
        if not info["station"]:
            raise FileNotFoundError("cannot infer the instance of this run")
        sdir = str(self.scenario_dir(info["station"], info["scenario"]))
        # v2 solver builds the fixed graph; legacy runs may need an old variant.
        # Graphs are always built with link resources: arc ids do not depend on them and
        # the checker then also reports head-on crossings of runs of the original model.
        order = [0] if info["kind"] == "v2" else [1, 2, 0]
        last_err = None
        for v in order:
            g = cached_graph(sdir, v)
            if num_arcs is not None and len(g.arcs) != num_arcs:
                continue
            try:
                decoded = decode_solution(g, train_paths)
                return g, decoded, v
            except PathError as e:
                last_err = e
        raise PathError(f"solution does not match the instance graph ({last_err})")

    def solution(self, run_id: str) -> dict:
        sol = self._raw_solution(run_id)
        paths = sol.get("train_paths") or []
        if not paths:
            return {"status": sol.get("status"), "objective": None, "trains": [], "conflicts": []}
        g, decoded, v = self._graph_for(run_id, paths, (sol.get("graph") or {}).get("num_arcs"))
        rejected = sum(t["rejected"] for t in decoded["trains"])
        return {
            "status": sol.get("status"),
            "objective": sol.get("objective_value"),
            "T": g.T,
            "t_s": g.t_s,
            "graph_variant": v,
            "stats": sol.get("stats"),
            "accepted": len(decoded["trains"]) - rejected,
            "rejected": rejected,
            **decoded,
        }

    def _trace_paths(self, run_id: str, kind: str, idx: int | None = None) -> dict:
        trace = self.run_dir(run_id) / "trace.jsonl"
        n = -1
        for ev in read_events(trace, strip_paths=False):
            if ev.get("ev") != kind:
                continue
            n += 1
            if idx is None or n == idx:
                return ev
        raise FileNotFoundError(f"{kind} #{idx} not found")

    def incumbent(self, run_id: str, idx: int) -> dict:
        ev = self._trace_paths(run_id, "incumbent", idx)
        g, decoded, _ = self._graph_for(run_id, ev["train_paths"])
        return {"t": ev["t"], "value": ev["value"], "source": ev["source"], "T": g.T, "t_s": g.t_s, **decoded}

    def lp(self, run_id: str) -> dict:
        ev = self._trace_paths(run_id, "lp")
        info = self.info(run_id)
        sdir = str(self.scenario_dir(info["station"], info["scenario"]))
        g = cached_graph(sdir, 0)
        inst = g.inst
        paths = []
        for p in ev["train_paths"]:
            d = decode_path(g, p["train_id"], p["arc_sequence"])
            d.pop("occupancy")
            paths.append({"train_id": p["train_id"], "flow": p.get("flow", 1.0),
                          "name": inst.trains[p["train_id"]].name, **d})
        fractional = sorted({p["train_id"] for p in paths if 1e-5 < p["flow"] < 1 - 1e-5})
        return {"t": ev["t"], "value": ev["value"], "T": g.T, "t_s": g.t_s,
                "paths": paths, "fractional_trains": fractional}
