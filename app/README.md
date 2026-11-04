# TUSP Visualizer

A web app to **see** what the Branch-and-Price does: pick a station and a scenario,
replay the solution from `t = 0` to the end on the station map, and follow the
convergence of the solver (bounds, column generation, search tree), live while it runs.

```
┌──── C++ solver (src/) ─────────┐   files    ┌──── FastAPI (app/backend) ───┐  HTTP/WS  ┌── React + D3 + ECharts ──┐
│ cli_tusp_solver ... -o <dir>   │ ─────────▶ │ instances, runs, decoding,   │ ────────▶ │ station map, simulation,  │
│   solution.json  trace.jsonl   │            │ launch / stop the solver     │           │ convergence, tree, bench  │
└────────────────────────────────┘            └──────────────────────────────┘           └───────────────────────────┘
```

## Quick start

Prerequisites: Python ≥ 3.10, Node ≥ 18, and (to solve, not just replay) Gurobi + CMake.

```bash
./app/run.sh
```

Then open <http://localhost:8000>. The script installs the Python/JS dependencies,
builds the frontend once and starts the server. Without a solver binary the app runs in
**replay mode** (existing results only).

### Enabling the solver

1. **Gurobi license.** With a Web License Service (WLS) license, create `~/gurobi.lic`
   (or point `GRB_LICENSE_FILE` to it). **Never put it inside the repository**: `*.lic` is git-ignored.
   ```
   WLSACCESSID=...
   WLSSECRET=...
   LICENSEID=...
   ```
2. **Build** (from the repository root, `GUROBI_HOME` must be set):
   ```bash
   mkdir -p build && cd build
   cmake -DCMAKE_BUILD_TYPE=Release ..
   cmake --build . -j
   ```
   The backend picks up `build/cli_tusp_solver` automatically (or set `TUSP_SOLVER=/path/to/cli_tusp_solver`).
3. Restart `./app/run.sh`: the top bar shows **solver ready**.

### Development mode

```bash
pip install -r app/backend/requirements.txt
uvicorn main:app --app-dir app/backend --reload --port 8000   # terminal 1
cd app/frontend && npm install && npm run dev                  # terminal 2 → http://localhost:5173
```

Tests: `cd app/backend && python -m pytest -q`.

## What you can do

| Page | Content |
| --- | --- |
| **Instances** | Grid of stations × scenarios with size, status (optimal / gap / not solved) and best objective. *Explore*, *Replay*, *Run*. |
| **Explore an instance** | Station map (automatic schematic layout, **editable by drag & drop**, saved to `instances/<station>/layout.json`), station load vs capacity, service demand vs capable nodes, arcs of the time-space graph, demand Gantt, all runs of the instance. |
| **Run** | Choose the method (**Branch-and-Price**, exact, **LNS**, heuristic, or **Both · benchmark**: B&P then LNS with the same time limit and model, then the side-by-side comparison opens; also the **⚖ B&P vs LNS** button of an instance page) and the model (**forbid head-on crossings on links**, on by default). Then set a time limit, or tick **Tune … parameters**. For B&P: CG gap per depth tier, heuristic frequency, diving (+ dive gap), restricted-master MIP (+ time limit), branching rule, node selection, Gurobi threads. For LNS (ALNS): neighbourhood share (initial / max), destroy operators, repair operators (greedy, regret, price-and-dive, dive + MIP), CG gap, bound pruning, MIP time, iteration budget, acceptance (simulated annealing start / end, or improvement only), segment length and reaction factor of the adaptive weights, iteration limit, root budget, seed. Presets for both. |
| **Simulation** | Animated station map (head-on crossings flash in red, with a "⚠ N crossings (next ▸)" button): trains move along the tracks during shunting, service progress rings, parking badges. Play / pause / step (← →, space), speed 2× → 60×. KPIs at t (in station, shunting, in service, services done, cost accrued), event log, time-space diagram, train Gantt, cost breakdown, node utilisation. Replay the **best solution**, **any intermediate incumbent**, or the **fractional root LP** ("ghost trains" with opacity = flow). Independent conflict check (✓ no conflict). |
| **Convergence** | LB/UB step curves vs time or nodes, incumbent markers by source (B&P node / diving / MIP), heuristic windows, DFS↔BBF switches, gap on a log scale, column pool and open nodes, **column generation per node** (RMP value vs Lagrangian bound, columns added, most negative reduced cost: the tailing-off effect is visible), time split master / pricing / heuristics, incumbents table. A solver-time slider replays the whole solve. |
| **ALNS** (Convergence tab of an LNS run) | KPIs (iterations, iterations/s, new bests, worse moves accepted, share cut by a bound, sub-master size, time per iteration). Search trajectory: every repaired candidate coloured by its outcome (new best / better than current / worse accepted by simulated annealing / rejected / pruned / failed) with the current solution, the best solution and the root bound. Outcomes per destroy and per repair operator, repair time share and ms per try, evolution of the destroy and repair weights, of k and of the temperature. Runs of the first LNS are still displayed. |
| **Search tree** (B&P runs) | The B&P tree growing over solver time, node colour = status, dashed ring = node being solved, node details (branching decision with service names, bound history). |
| **Solver log** | Console output of the run (streamed live). |
| **Benchmark** | **Watch the methods on the station**: choose a benchmarked instance and two of its runs (default: the B&P and LNS runs of its latest benchmark), they play side by side on two station maps with one shared clock (same colour = same train), with live KPIs for each, the trains refused by only one of them, the trains whose cost differs, the cost breakdowns and the two schedules train by train (≠ marks trains accepted by one method and refused by the other). Clicking a row of the B&P vs LNS table opens it. **Run a benchmark**: tick instances × methods, model and time limit; the runs are executed one after the other (progress, cancel). **B&P vs LNS** table: best objective of each method per instance, gap to the best lower bound known for the instance, time to first solution, winner. Difficulty heatmap (B&P / LNS / best of both), gap vs size, convergence curves of up to 6 runs, sortable table of all runs (method, model, parameters). Filters: corrected model only, one batch. |

A running solve can be **stopped** from the run page: the solver receives `SIGTERM`, finishes its
current step and still writes its best solution.

## Outputs of the solver

Each run writes into `results/runs/<run id>/`:

* `solution.json`: status, stats, graph metadata (`T`, `t_s`, number of arcs) and, per train, its `arc_sequence`.
* `trace.jsonl`: one JSON event per line, flushed immediately:

  | `ev` | fields |
  | --- | --- |
  | `start` | instance, trains, arcs, T, t_s, params |
  | `bounds` | t, it, node, depth, lb, ub, gap, pool, open, t_master, t_pricing, t_heur |
  | `cg` | ctx (`node` / `dive`), node, it, t, rmp, lagr, added, pool, min_rc |
  | `node` | id, parent, depth, status, lb, label, t |
  | `branch` | parent, rule, train, service, split_time, divergence_time, children[{id, label}] |
  | `incumbent` | t, value, source (`node` / `diving` / `mip`), train_paths |
  | `lp` | root LP solution: value, train_paths with `flow` |
  | `heuristic` | t, duration, ub_before, ub_after |
  | `strategy` | t, value (`DFS` / `BBF`) |
  | `lns` | (LNS) it, t, dur, op (destroy), rep (repair), k, frac, lb (neighbourhood bound), value, prev, cur, ub, temp, thr, result, cg_it, dive_steps, rows, cols, weights, rweights |
  | `end` | status, root_lb, lb, ub, gap, ttfs, nodes (B&P nodes / LNS iterations), pruned, columns, timings (+ LNS destroy / repair operator statistics) |
* `dynamics.csv`: the former LB/UB timeline (kept for compatibility), `stdout.log`, `gurobi_log.txt`, `run.json` (written by the app: instance, parameters, state).

The checker always reports two kinds of violations, whatever the model of the run:
**node conflicts** (two trains on one node in the same time step) and **head-on crossings**
(two trains on one link in the same time step, allowed by the original model). See
`docs/MODEL_AND_METHODS.md` for the link constraint and the ALNS.

The backend decodes arc ids by rebuilding the time-space graph in Python
(`app/backend/tusp/graph.py`, same arc order as `GraphBuilder`); every path is validated
(contiguity, ownership, cost equal to the solver's). Results of the **old solver**
(`results/solutions`, `results/dynamics`) are also supported, with the bounds timeline only.
