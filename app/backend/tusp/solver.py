"""Launch / stop the C++ solver (cli_tusp_solver) as a subprocess."""
from __future__ import annotations

import json
import os
import re
import shutil
import signal
import subprocess
import sys
import threading
import time
from datetime import datetime
from pathlib import Path
from typing import Literal

from pydantic import BaseModel, Field, field_validator


LNS_OPERATORS = ("random", "time", "related", "dual", "rejected")


class SolverParams(BaseModel):
    """Mirror of src/solver/SolverParams.h (defaults = solver defaults,
    except the time limit which is shorter for interactive use)."""
    method: Literal["bp", "lns"] = "bp"
    link_conflicts: bool = True
    time_limit_s: float = Field(300, gt=0, le=86400)
    cg_gaps: list[float] = Field(default_factory=lambda: [0.0, 0.001, 0.005, 0.01])
    heuristic_every: int = Field(5, ge=0, le=10000)
    diving: bool = True
    dive_cg_gap: float = Field(0.05, ge=0, le=1)
    mip_heuristic: bool = True
    mip_time_s: float = Field(15, gt=0, le=3600)
    branching: Literal["auto", "quantum"] = "auto"
    search: Literal["bbf", "dfs"] = "bbf"
    threads: int = Field(0, ge=0, le=256)
    # LNS
    lns_init_frac: float = Field(0.3, gt=0, le=1)
    lns_max_frac: float = Field(0.7, gt=0, le=1)
    lns_mip_time_s: float = Field(5, ge=0, le=3600)
    lns_root_mip_time_s: float = Field(10, ge=0, le=3600)
    lns_root_time_frac: float = Field(0.3, gt=0, le=1)
    lns_cg_gap: float = Field(0.001, ge=0, le=1)
    lns_bound_pruning: bool = True
    lns_operators: list[Literal["random", "time", "related", "dual", "rejected"]] = Field(
        default_factory=lambda: list(LNS_OPERATORS))
    lns_max_iter: int = Field(0, ge=0)
    seed: int = Field(1, ge=0)

    @field_validator("lns_operators")
    @classmethod
    def _some_operator(cls, v):
        if not v:
            raise ValueError("choose at least one LNS operator")
        return v

    @field_validator("cg_gaps")
    @classmethod
    def _four_gaps(cls, v):
        if len(v) != 4 or any(g < 0 or g > 1 for g in v):
            raise ValueError("cg_gaps needs 4 values in [0, 1]")
        return v

    def to_args(self) -> list[str]:
        args = [
            "-t", repr(self.time_limit_s),
            "--cg-gaps", ",".join(repr(g) for g in self.cg_gaps),
            "--heur-every", str(self.heuristic_every),
            "--dive-gap", repr(self.dive_cg_gap),
            "--mip-time", repr(self.mip_time_s),
            "--branching", self.branching,
            "--threads", str(self.threads),
        ]
        if not self.diving:
            args.append("--no-diving")
        if not self.mip_heuristic:
            args.append("--no-mip")
        if self.search == "dfs":
            args.append("--dfs")
        if not self.link_conflicts:
            args.append("--no-link-conflicts")
        args += ["--method", self.method]
        if self.method == "lns":
            args += [
                "--lns-frac", repr(self.lns_init_frac), "--lns-max-frac", repr(self.lns_max_frac),
                "--lns-mip-time", repr(self.lns_mip_time_s), "--lns-root-mip-time", repr(self.lns_root_mip_time_s),
                "--lns-root-frac", repr(self.lns_root_time_frac), "--lns-cg-gap", repr(self.lns_cg_gap),
                "--lns-ops", ",".join(self.lns_operators), "--lns-max-iter", str(self.lns_max_iter),
                "--seed", str(self.seed),
            ]
            if not self.lns_bound_pruning:
                args.append("--lns-no-prune")
        return args


def find_solver(root: Path) -> Path | None:
    env = os.environ.get("TUSP_SOLVER")
    candidates = [Path(env)] if env else []
    exe = "cli_tusp_solver.exe" if sys.platform == "win32" else "cli_tusp_solver"
    candidates += [root / "build" / exe, root / "build" / "Release" / exe]
    for c in candidates:
        if c.is_file() and os.access(c, os.X_OK):
            return c.resolve()
    found = shutil.which("cli_tusp_solver")
    return Path(found) if found else None


def gurobi_status() -> dict:
    lic = os.environ.get("GRB_LICENSE_FILE")
    lic_candidates = [Path(lic)] if lic else []
    lic_candidates += [Path.home() / "gurobi.lic", Path("/opt/gurobi/gurobi.lic")]
    return {
        "gurobi_home": os.environ.get("GUROBI_HOME"),
        "license_found": any(p.is_file() for p in lic_candidates),
    }


def slug(s: str) -> str:
    return re.sub(r"[^A-Za-z0-9]+", "_", s).strip("_").lower()


class RunningSolve:
    def __init__(self, run_id: str, proc: subprocess.Popen, run_dir: Path, log):
        self.run_id = run_id
        self.proc = proc
        self.run_dir = run_dir
        self.log = log
        self.stop_requested = False


class SolverManager:
    def __init__(self, root: Path, runs_dir: Path):
        self.root = root
        self.runs_dir = runs_dir
        self.active: dict[str, RunningSolve] = {}
        self.batches: dict[str, dict] = {}
        self._lock = threading.Lock()

    def solver_path(self) -> Path | None:
        return find_solver(self.root)

    def _write_meta(self, run_dir: Path, **updates):
        f = run_dir / "run.json"
        meta = {}
        if f.exists():
            try:
                meta = json.loads(f.read_text(encoding="utf-8"))
            except json.JSONDecodeError:
                meta = {}
        meta.update(updates)
        f.write_text(json.dumps(meta, indent=2), encoding="utf-8")

    def start(self, station: str, scenario: str, scenario_dir: Path, params: SolverParams, batch: str | None = None) -> str:
        exe = self.solver_path()
        if exe is None:
            raise RuntimeError(
                "Solver binary not found. Build it (see README) or set TUSP_SOLVER=/path/to/cli_tusp_solver."
            )
        stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
        k = re.sub(r"\D", "", scenario) or slug(scenario)
        run_id = f"{slug(station)}_sce_{k}_{params.method}_{stamp}"
        n = 1
        while (self.runs_dir / run_id).exists():   # several runs started in the same second
            n += 1
            run_id = f"{slug(station)}_sce_{k}_{params.method}_{stamp}-{n}"
        run_dir = self.runs_dir / run_id
        run_dir.mkdir(parents=True, exist_ok=False)
        self._write_meta(
            run_dir, id=run_id, station=station, scenario=scenario, params=params.model_dump(),
            created=datetime.now().isoformat(timespec="seconds"), state="running", batch=batch,
        )
        cmd = [str(exe), "-f", str(scenario_dir) + os.sep, "-n", run_id, "-o", str(run_dir), *params.to_args()]
        log = open(run_dir / "stdout.log", "w", encoding="utf-8")
        popen_kw = {}
        if sys.platform != "win32":
            popen_kw["start_new_session"] = True  # Ctrl-C in the server terminal does not hit the solver
        proc = subprocess.Popen(cmd, cwd=exe.parent, stdout=log, stderr=subprocess.STDOUT, **popen_kw)
        self._write_meta(run_dir, cmd=cmd, pid=proc.pid)
        with self._lock:
            self.active[run_id] = RunningSolve(run_id, proc, run_dir, log)
        threading.Thread(target=self._wait, args=(run_id,), daemon=True).start()
        return run_id

    def _wait(self, run_id: str):
        rs = self.active[run_id]
        code = rs.proc.wait()
        rs.log.close()
        state = "stopped" if rs.stop_requested else ("finished" if code == 0 else "failed")
        self._write_meta(rs.run_dir, state=state, returncode=code,
                         finished=datetime.now().isoformat(timespec="seconds"))
        with self._lock:
            self.active.pop(run_id, None)

    def stop(self, run_id: str) -> bool:
        rs = self.active.get(run_id)
        if not rs:
            return False
        rs.stop_requested = True
        # SIGTERM: the solver finishes its current step and still exports the incumbent.
        rs.proc.terminate()

        def _kill_later():
            time.sleep(60)
            if rs.proc.poll() is None:
                rs.proc.kill()

        threading.Thread(target=_kill_later, daemon=True).start()
        return True

    # ------------------------------------------------------------- batches --
    def start_batch(self, name: str, jobs: list[dict], resolve_dir) -> str:
        """jobs: [{station, scenario, params: SolverParams}]; run one after the other."""
        bid = datetime.now().strftime("%Y%m%d-%H%M%S") + f"-{len(self.batches)}"
        batch = {"id": bid, "name": name or bid, "created": datetime.now().isoformat(timespec="seconds"),
                 "jobs": [{"station": j["station"], "scenario": j["scenario"], "params": j["params"].model_dump(),
                           "state": "pending", "run_id": None, "error": None} for j in jobs],
                 "cancelled": False}
        with self._lock:
            self.batches[bid] = batch
        self._save_batch(batch)
        t = threading.Thread(target=self._run_batch, args=(bid, resolve_dir), daemon=True)
        t.start()
        return bid

    def _save_batch(self, batch: dict):
        d = self.runs_dir.parent / "batches"
        d.mkdir(parents=True, exist_ok=True)
        (d / f"{batch['id']}.json").write_text(json.dumps(batch, indent=1), encoding="utf-8")

    def _run_batch(self, bid: str, resolve_dir):
        batch = self.batches[bid]
        for job in batch["jobs"]:
            if batch["cancelled"]:
                job["state"] = "cancelled"
                continue
            try:
                sdir = resolve_dir(job["station"], job["scenario"])
                job["run_id"] = self.start(job["station"], job["scenario"], sdir, SolverParams(**job["params"]), batch=bid)
                job["state"] = "running"
                self._save_batch(batch)
                while self.is_running(job["run_id"]):
                    time.sleep(1)
                job["state"] = "done"
            except Exception as e:  # noqa: BLE001 — keep the batch going
                job["state"] = "failed"
                job["error"] = str(e)
            self._save_batch(batch)

    def cancel_batch(self, bid: str) -> bool:
        b = self.batches.get(bid)
        if not b:
            return False
        b["cancelled"] = True
        for j in b["jobs"]:
            if j["state"] == "running" and j["run_id"]:
                self.stop(j["run_id"])
        return True

    def list_batches(self) -> list[dict]:
        d = self.runs_dir.parent / "batches"
        out = {}
        if d.exists():
            for f in d.glob("*.json"):
                try:
                    b = json.loads(f.read_text(encoding="utf-8"))
                    out[b["id"]] = b
                except (OSError, json.JSONDecodeError):
                    pass
        out.update(self.batches)   # live state wins
        for b in out.values():
            if b["id"] not in self.batches:   # from a previous server session
                for j in b["jobs"]:
                    if j["state"] in ("pending", "running"):
                        j["state"] = "interrupted"
        return sorted(out.values(), key=lambda b: b["created"], reverse=True)

    def is_running(self, run_id: str) -> bool:
        return run_id in self.active

    def stdout_tail(self, run_id: str, n: int = 200) -> list[str]:
        f = self.runs_dir / run_id / "stdout.log"
        if not f.exists():
            return []
        lines = f.read_text(encoding="utf-8", errors="replace").splitlines()
        return [re.sub(r"\x1b\[[0-9;]*m", "", l) for l in lines[-n:]]
