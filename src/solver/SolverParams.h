#pragma once

#include <string>
#include <vector>

enum class BranchingRule {
    AUTO,     // service_split first, quantum_train as fallback (historical behaviour)
    QUANTUM   // quantum_train only
};

/*
Every knob that controls the convergence of the Branch-and-Price.
Defaults reproduce the historical behaviour of the solver.
All of them are exposed through the CLI (see cli_B&P.cpp --help) and the web app.
*/
struct SolverParams {
    double time_limit_s = 3600.0;

    // Column generation stops when the Lagrangian gap |RMP - LB| / LB <= gap.
    // Tiers: root node, depth <= 5, depth <= 10, deeper.
    std::vector<double> cg_gaps = {0.0, 0.001, 0.005, 0.01};

    // Primal heuristics are launched every `heuristic_every` B&P nodes (0 = never).
    int heuristic_every = 5;
    bool diving = true;
    double dive_cg_gap = 0.05;      // CG termination gap used while diving
    bool mip_heuristic = true;
    double mip_time_s = 15.0;       // time limit of the restricted-master MIP

    BranchingRule branching = BranchingRule::AUTO;
    bool start_dfs = false;          // initial node selection: DFS (else best-bound-first)
    int threads = 0;                 // Gurobi threads (0 = Gurobi default)

    std::string out_dir;             // where solution.json / trace.jsonl / dynamics.csv go
    bool trace = true;               // write trace.jsonl (event stream for the web app)
    bool lp_snapshot = true;         // dump the fractional root LP solution in the trace
};
