## Projet du Cours PORT 2026 - MPRO

### 1. Build (Gurobi required)
Hello everybody

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

Headers are located under `src/builder`,`src/data`, `src/graph`, `src/parser`, `src/solver` and are all included in the CMakeLists

### Run Branch and Price CLI
It is possible to run the solver on a specified instance using the cli tool.
Usage
```
./cli_tusp_solver -f -f <filepath> [-t <time limit in seconds>] [-n <instance name>]
```
Arguments (optional):
- `-t or --time `: time limit for the branch and price solver in second (default 3600)
- `-n or --name` : name given to the current instance and used in the different result files (default filepath)
- `-h or --help`: prints back a simple help message

Example:
```
./cli_tusp_solver -f "../instances/large station/scenario 1" -n large_station_sce_1
```
Remark:
-The CLI produces file in predetermined spots (`results/dynamics` and `results/solutions`) if you don't rename the target it will erase the previous files at that spot. Be careful not to lose your precious data by launching twice the same instance with the same target name.
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
Each run on an instance generates two types of files:
- a `solution_instance_name.json` file that gives back basic information about the solver generated solution.
- a `dynamics_instance_name.csv` file that gives back some information about the state of the solver at different points in time during solving
- the benchmark executable creates a `summary_benchmark.csv` file that lists for each file specified in the benchmark the following statistics :
  -the instance name
  -the number of trains
  -the number of arcs
  -the root relaxation lower bound
  -the final lower bound
  -the final upper bound
  -the final gap
  -the total time of solve
  -the time to first solution (TTFS)
  -the number of explored nodes
  -the time spent in the master problem
  -the time spent in pricing
  -the time spent in primal heuristics (it contains time from master and pricing as it also uses column generation)
