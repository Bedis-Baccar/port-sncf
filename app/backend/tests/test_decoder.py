"""Checks that the Python rebuild of the time-space graph matches the C++ solver
and that every stored solution decodes into a conflict-free schedule."""
import csv
import json
from pathlib import Path

import pytest

from tusp.graph import TimeSpaceGraph
from tusp.instance import load_instance
from tusp.runs import guess_instance
from tusp.solution import VARIANTS, PathError, decode_solution

ROOT = Path(__file__).resolve().parents[3]
SOLUTIONS = sorted((ROOT / "results" / "solutions").glob("solution_*.json"))
CROSSINGS: dict[str, int] = {}


def scenario_dir(name):
    station, scenario = guess_instance(name)
    return ROOT / "instances" / station / scenario


def test_arc_counts_match_old_benchmark():
    """The old solver's arc counts are reproduced by one of the graph variants
    (the out-of-bounds read made some runs add arcs on in/out nodes)."""
    with open(ROOT / "results" / "summary_benchmark.csv", encoding="utf-8-sig") as f:
        rows = list(csv.DictReader(f))
    assert rows
    for r in rows:
        inst = load_instance(scenario_dir(r["Instance"]))
        counts = {len(TimeSpaceGraph(inst, **v).arcs) for v in VARIANTS}
        assert int(r["Arcs"]) in counts, r["Instance"]


@pytest.mark.parametrize("path", SOLUTIONS, ids=lambda p: p.stem)
def test_legacy_solutions_decode(path):
    sol = json.loads(path.read_text())
    inst = load_instance(scenario_dir(path.stem[len("solution_"):]))
    for v in VARIANTS:
        try:
            decoded = decode_solution(TimeSpaceGraph(inst, **v), sol["train_paths"])
        except PathError:
            continue
        assert sum(t["cost"] for t in decoded["trains"]) == sol["objective_value"]
        # the original model respects node conflicts...
        assert [c for c in decoded["conflicts"] if c["kind"] == "node"] == []
        # ...but allows head-on crossings, which the checker now reports
        CROSSINGS[path.stem] = sum(c["kind"] == "link" for c in decoded["conflicts"])
        for t in decoded["trains"]:
            if not t["rejected"]:
                assert sorted(t["services_done"]) == sorted(t["required_services"])
                assert t["segments"][0]["kind"] == "arrive" and t["segments"][-1]["kind"] == "depart"
                assert t["segments"][0]["t0"] == t["a_t"] and t["segments"][-1]["t0"] == t["d_t"]
        return
    pytest.fail("no graph variant decodes this solution")


def test_broken_path_is_rejected():
    inst = load_instance(ROOT / "instances" / "small station" / "scenario 1")
    g = TimeSpaceGraph(inst)
    sol = json.loads((ROOT / "results" / "solutions" / "solution_small_station_sce_1.json").read_text())
    tp = dict(sol["train_paths"][0])
    tp["arc_sequence"] = tp["arc_sequence"][:3] + tp["arc_sequence"][4:]
    with pytest.raises(PathError):
        decode_solution(g, [tp])


def test_old_model_crossings_are_all_detected():
    """89 head-on crossings were counted by hand in the 10 old solutions."""
    if len(CROSSINGS) < len(SOLUTIONS):
        for p in SOLUTIONS:
            test_legacy_solutions_decode(p)
    assert sum(CROSSINGS.values()) == 89


def test_link_resources_do_not_change_arc_ids():
    inst = load_instance(ROOT / "instances" / "medium station" / "scenario 2")
    on, off = TimeSpaceGraph(inst, link_conflicts=True), TimeSpaceGraph(inst, link_conflicts=False)
    assert [(a.type, a.frm, a.to, a.t0, a.t1) for a in on.arcs] == [(a.type, a.frm, a.to, a.t0, a.t1) for a in off.arcs]
    # one resource per unordered link, used by both directions of every move
    assert len(on.resource_names) == len(inst.pnodes) + len(inst.plinks)
