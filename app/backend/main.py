"""TUSP Visualizer — FastAPI backend.

Run from the repository root:
    uvicorn main:app --app-dir app/backend --reload
"""
from __future__ import annotations

import asyncio
import json
import os
import shutil
from functools import reduce
from math import gcd
from pathlib import Path

from fastapi import FastAPI, HTTPException, WebSocket, WebSocketDisconnect
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import FileResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel

from tusp.layout import layout_file, load_layout
from tusp.runs import Paths, RunStore, cached_graph, cached_instance, strip_event
from tusp.solution import PathError
from tusp.solver import SolverManager, SolverParams, gurobi_status

ROOT = Path(os.environ.get("TUSP_ROOT", Path(__file__).resolve().parents[2]))
paths = Paths(ROOT)
store = RunStore(paths)
manager = SolverManager(ROOT, paths.runs)

app = FastAPI(title="TUSP Visualizer API")
app.add_middleware(CORSMiddleware, allow_origins=["http://localhost:5173", "http://127.0.0.1:5173"],
                   allow_methods=["*"], allow_headers=["*"])


def _not_found(e: Exception):
    raise HTTPException(status_code=404, detail=str(e))


def _natural(s: str):
    import re
    return [int(t) if t.isdigit() else t for t in re.split(r"(\d+)", s)]


# ------------------------------------------------------------------ health --
@app.get("/api/health")
def health():
    exe = manager.solver_path()
    return {
        "root": str(ROOT),
        "solver_path": str(exe) if exe else None,
        "solver_available": exe is not None,
        **gurobi_status(),
        "active_runs": list(manager.active),
    }


# --------------------------------------------------------------- instances --
def _time_info(inst) -> tuple[int, int]:
    vals = [v for t in inst.trains for v in (t.a_t, t.d_t)]
    vals += [s.duration for s in inst.services] + [tt.t_turn for tt in inst.train_types]
    vals += [l.length for l in inst.plinks]
    return max((t.d_t for t in inst.trains), default=0), reduce(gcd, vals, 0)


@app.get("/api/instances")
def list_instances():
    runs = _runs_with_state()
    stations = []
    if not paths.instances.exists():
        return stations
    for sdir in sorted((d for d in paths.instances.iterdir() if d.is_dir()), key=lambda d: _natural(d.name)):
        scenarios = []
        for cdir in sorted((d for d in sdir.iterdir() if d.is_dir()), key=lambda d: _natural(d.name)):
            try:
                inst = cached_instance(str(cdir.resolve()))
            except Exception as e:  # noqa: BLE001 — show broken instances instead of failing the list
                scenarios.append({"scenario": cdir.name, "error": str(e)})
                continue
            T, t_s = _time_info(inst)
            mine = [r for r in runs if r["station"] == sdir.name and r["scenario"] == cdir.name]
            scenarios.append({
                "scenario": cdir.name,
                "trains": len(inst.trains),
                "services_required": sum(len(t.required_services) for t in inst.trains),
                "T": T, "t_s": t_s,
                "runs": mine,
            })
        if scenarios:
            first = next((s for s in scenarios if "error" not in s), None)
            inst0 = cached_instance(str((sdir / first["scenario"]).resolve())) if first else None
            stations.append({
                "station": sdir.name,
                "nodes": len(inst0.pnodes) if inst0 else None,
                "links": len(inst0.plinks) if inst0 else None,
                "scenarios": scenarios,
            })
    return stations


@app.get("/api/instances/{station}/{scenario}")
def get_instance(station: str, scenario: str, graph: bool = True):
    try:
        sdir = store.scenario_dir(station, scenario)
    except FileNotFoundError as e:
        _not_found(e)
    inst = cached_instance(str(sdir))
    T, t_s = _time_info(inst)
    layout, custom = load_layout(inst, sdir.parent)
    out = {**inst.to_dict(), "T": T, "t_s": t_s, "layout": layout, "layout_custom": custom}
    if graph:
        g = cached_graph(str(sdir), 0)
        out["arc_counts"] = g.arc_type_counts()
        out["num_arcs"] = len(g.arcs)
    return out


class LayoutBody(BaseModel):
    positions: dict[str, dict[str, float]]


@app.put("/api/instances/{station}/layout")
def save_layout(station: str, body: LayoutBody):
    sdir = (paths.instances / station).resolve()
    if paths.instances.resolve() not in sdir.parents or not sdir.is_dir():
        raise HTTPException(404, "unknown station")
    layout_file(sdir).write_text(json.dumps(body.positions, indent=1), encoding="utf-8")
    return {"ok": True}


@app.delete("/api/instances/{station}/layout")
def reset_layout(station: str):
    sdir = (paths.instances / station).resolve()
    if paths.instances.resolve() not in sdir.parents:
        raise HTTPException(404, "unknown station")
    f = layout_file(sdir)
    if f.exists():
        f.unlink()
    return {"ok": True}


# -------------------------------------------------------------------- runs --
def _runs_with_state() -> list[dict]:
    runs = store.list_runs()
    for r in runs:
        if r["state"] == "running" and not manager.is_running(r["id"]):
            r["state"] = "finished" if r["has_solution"] else "interrupted"
    return runs


@app.get("/api/runs")
def list_runs():
    return _runs_with_state()


@app.get("/api/runs/{run_id}")
def get_run(run_id: str):
    try:
        info = store.info(run_id)
    except FileNotFoundError as e:
        _not_found(e)
    info["running"] = manager.is_running(run_id)
    if info["state"] == "running" and not info["running"]:
        info["state"] = "finished" if info["has_solution"] else "interrupted"
    return info


@app.get("/api/runs/{run_id}/events")
def get_events(run_id: str):
    try:
        return store.events(run_id)
    except FileNotFoundError as e:
        _not_found(e)


@app.get("/api/runs/{run_id}/solution")
def get_solution(run_id: str):
    try:
        return store.solution(run_id)
    except FileNotFoundError as e:
        _not_found(e)
    except PathError as e:
        raise HTTPException(422, str(e))


@app.get("/api/runs/{run_id}/incumbents/{idx}")
def get_incumbent(run_id: str, idx: int):
    try:
        return store.incumbent(run_id, idx)
    except FileNotFoundError as e:
        _not_found(e)
    except PathError as e:
        raise HTTPException(422, str(e))


@app.get("/api/runs/{run_id}/lp")
def get_lp(run_id: str):
    try:
        return store.lp(run_id)
    except FileNotFoundError as e:
        _not_found(e)
    except PathError as e:
        raise HTTPException(422, str(e))


@app.get("/api/runs/{run_id}/log")
def get_log(run_id: str, n: int = 300):
    return {"lines": manager.stdout_tail(run_id, n)}


class RunRequest(BaseModel):
    station: str
    scenario: str
    params: SolverParams = SolverParams()


@app.post("/api/runs")
def start_run(req: RunRequest):
    try:
        sdir = store.scenario_dir(req.station, req.scenario)
    except FileNotFoundError as e:
        _not_found(e)
    try:
        run_id = manager.start(req.station, req.scenario, sdir, req.params)
    except RuntimeError as e:
        raise HTTPException(503, str(e))
    return {"id": run_id}


@app.post("/api/runs/{run_id}/stop")
def stop_run(run_id: str):
    if not manager.stop(run_id):
        raise HTTPException(409, "run is not active")
    return {"ok": True}


@app.delete("/api/runs/{run_id}")
def delete_run(run_id: str):
    if manager.is_running(run_id):
        raise HTTPException(409, "stop the run first")
    try:
        d = store.run_dir(run_id)
    except FileNotFoundError as e:
        _not_found(e)
    shutil.rmtree(d)
    return {"ok": True}


@app.websocket("/api/runs/{run_id}/live")
async def live(ws: WebSocket, run_id: str):
    """Streams trace events (and solver stdout) of a run as they are written."""
    await ws.accept()
    try:
        d = store.run_dir(run_id)
    except FileNotFoundError:
        await ws.send_json({"type": "error", "detail": "unknown run"})
        await ws.close()
        return
    trace, log = d / "trace.jsonl", d / "stdout.log"
    pos = {"trace": 0, "log": 0}
    buf = {"trace": "", "log": ""}
    counter = [0]

    def read_new(key: str, f: Path) -> list[str]:
        if not f.exists():
            return []
        with open(f, "rb") as fh:
            fh.seek(pos[key])
            chunk = fh.read()
            pos[key] = fh.tell()
        text = buf[key] + chunk.decode("utf-8", "replace")
        lines = text.split("\n")
        buf[key] = lines.pop()  # keep a partially written line for the next round
        return [l for l in lines if l.strip()]

    try:
        while True:
            running = manager.is_running(run_id)
            events = []
            for line in read_new("trace", trace):
                try:
                    events.append(strip_event(json.loads(line), counter))
                except json.JSONDecodeError:
                    pass
            log_lines = [__import__("re").sub(r"\x1b\[[0-9;]*m", "", l) for l in read_new("log", log)]
            if events:
                await ws.send_json({"type": "events", "events": events})
            if log_lines:
                await ws.send_json({"type": "log", "lines": log_lines})
            if not running and not events and not log_lines:
                await ws.send_json({"type": "done"})
                break
            await asyncio.sleep(0.4)
    except WebSocketDisconnect:
        return
    await ws.close()


# ------------------------------------------------------------ static files --
DIST = ROOT / "app" / "frontend" / "dist"
if DIST.exists():
    app.mount("/assets", StaticFiles(directory=DIST / "assets"), name="assets")

    @app.get("/{path:path}")
    def spa(path: str):
        if path.startswith("api/"):
            raise HTTPException(404, "unknown API route")
        f = (DIST / path).resolve()
        if path and DIST.resolve() in f.parents and f.is_file():
            return FileResponse(f)
        return FileResponse(DIST / "index.html")
