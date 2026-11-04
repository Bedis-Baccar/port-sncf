## Projet du Cours PORT 2026 - MPRO

> **Visual app:** `./app/run.sh` then open http://localhost:8000 to explore instances, replay solutions on the
> station map from t = 0 to the end, run the solver with custom convergence parameters and follow its
> convergence live. See [`app/README.md`](app/README.md).

### 1. Build (Gurobi required)

Prereqs:
- `g++` and `cmake` available 
- `GUROBI_HOME` set (headers/libs must be discoverable)

**Build Instructions**
First, create and enter your build directory:
```bash
mkdir build
cd build
```

Then, you can choose between two build configurations:

**Option A: Release Build (Default)**
This mode applies aggressive optimizations (`-O3`, `-march=native`) and Link Time Optimization (LTO) for maximum performance during benchmarking.
```bash
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build .
```

**Option B: Debug Build**
This mode disables optimizations (`-O0`), generates debug information (`-g`), and enables AddressSanitizer to help track down memory leaks and bugs during development.
```bash
cmake -DCMAKE_BUILD_TYPE=Debug ..
cmake --build .
```

Headers are located under `src/builder`,`src/data`, `src/graph`, `src/parser`, `src/solver` and are all included in the CMakeLists.

### Run Branch and Price CLI
Usage
```
./cli_tusp_solver -f <scenario dir> [-n <name>] [-t <seconds>] [-o <output dir>] [convergence options]
```
Arguments (optional):
- `-t, --time`: time limit in seconds (default 3600)
- `-n, --name`: instance name used in the output (default: the path)
- `-o, --out`: output directory (default `../results/runs/<name>`)
- `--cg-gaps a,b,c,d`: column generation stopping gaps at the root, depth <= 5, depth <= 10, deeper (default `0,0.001,0.005,0.01`)
- `--heur-every n`: primal heuristics every n nodes, 0 = never (default 5)
- `--no-diving`, `--dive-gap g` (default 0.05): diving heuristic
- `--no-mip`, `--mip-time s` (default 15): MIP on the restricted master
- `--branching auto|quantum`: service split with quantum fallback (default) or quantum only
- `--dfs`: depth-first node selection (default best bound first)
- `--threads n`: Gurobi threads (default 0 = Gurobi's choice)
- `--no-trace`: do not write `trace.jsonl`
- `-h, --help`

Example:
```
./cli_tusp_solver -f "../instances/large station/scenario 1" -n large_station_sce_1 -t 600 --heur-every 2
```
`Ctrl-C` stops the solver cleanly: the best solution found so far is still exported.

### Run Branch and Price Benchmark
It is possible to run the solver on a set of specified instance by editing its main entry point `src/benchmark_B&P.cpp`
Usage
```
./benchmark_tusp_solver 
```

The following information must be provided in the entry point:
-The complete list of instance paths as a vector<string> instance_paths
-The complete list of name of the targeted instance to be used as a vector<string> instance_name
-The desired time limit as a double time_limit_s 

Remarks :
-For each instance instance_paths[i] and instance_name[i] must refer to the same instance
-The benchmark will produce output files in predetermined spots (`results/dynamics` and `results/solutions`), if you launch it twice it will (probably) erase previous files make sure to save previous results before launching a new benchmark

Example:
```
```

## Outputs
Each run writes into `results/runs/<name>/`:
- `solution.json`: status, statistics and, for each train, its path (`arc_sequence`) in the time-space graph.
- `trace.jsonl`: the event stream of the solve (column generation iterations, bounds, tree nodes, branching decisions, incumbents, root LP). Its format is documented in `app/README.md`.
- `dynamics.csv`: the LB/UB timeline.
- the benchmark executable also creates `results/summary_benchmark.csv` that lists for each instance:
  the name, the number of trains and arcs, the root relaxation lower bound, the final lower and upper bounds,
  the final gap, the total time, the time to first solution (TTFS), the number of explored nodes and the time
  spent in the master problem, in pricing and in the primal heuristics (which also includes master and pricing time
  because they use column generation).

Results of the previous version (`results/solutions`, `results/dynamics`) are kept and can still be replayed in the app.
