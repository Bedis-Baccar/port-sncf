from fastapi.testclient import TestClient

import main

client = TestClient(main.app)


def test_instances_listed():
    stations = {s["station"]: s for s in client.get("/api/instances").json()}
    assert {"small station", "medium station", "large station", "variant station"} <= set(stations)
    assert len(stations["small station"]["scenarios"]) == 5


def test_instance_detail_has_layout_for_every_node():
    inst = client.get("/api/instances/small station/scenario 1").json()
    assert set(inst["layout"]) == {n["name"] for n in inst["nodes"]}
    assert inst["num_arcs"] == 14043


def test_legacy_run_replay():
    sol = client.get("/api/runs/legacy:small_station_sce_1/solution").json()
    assert sol["objective"] == 1965 and sol["rejected"] == 0
    kinds = [c["kind"] for c in sol["conflicts"]]
    assert kinds == ["link"]  # the old model let T0 and T7 cross on r2-1|t3 at t=75
    events = client.get("/api/runs/legacy:small_station_sce_1/events").json()
    assert events[-1]["ev"] == "end"


def test_unknown_or_unsafe_paths_are_404():
    assert client.get("/api/runs/nope/solution").status_code == 404
    assert client.get("/api/runs/..%2F..%2Fetc/events").status_code == 404
    assert client.get("/api/instances/..%2F..%2Fsrc/x").status_code == 404


def test_params_validation():
    r = client.post("/api/runs", json={"station": "small station", "scenario": "scenario 1",
                                       "params": {"cg_gaps": [0, 1]}})
    assert r.status_code == 422
