#pragma once

#include <string>
#include <vector>
// (used by both solve methods: BranchAndPriceSolver and LnsSolver)

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

    // Model: forbid two trains on the same physical link in the same time step
    // (in particular head-on crossings). Used by GraphBuilder; false = original model.
    bool link_conflicts = true;

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

    // ---- Solve method ----------------------------------------------------------
    std::string method = "bp";       // "bp" = Branch-and-Price, "lns" = Large Neighbourhood Search

    // ---- LNS (method = "lns") ---------------------------------------------------
    double lns_init_frac = 0.3;      // initial share of trains freed in a neighbourhood
    double lns_max_frac = 0.7;       // the share grows up to this value, then restarts (diversification)
    double lns_mip_time_s = 5.0;     // time limit of the optional MIP polish of a neighbourhood (0 = off)
    double lns_cg_gap = 0.001;       // CG termination gap inside a neighbourhood
    double lns_root_mip_time_s = 10.0; // time limit of the MIP on the root columns
    double lns_root_time_frac = 0.3;   // share of the time limit the root column generation may use
    int lns_seed = 1;
    bool lns_bound_pruning = true;   // skip the MIP when the neighbourhood's Lagrangian bound >= UB - 1
    int lns_max_iter = 0;            // 0 = until the time limit
    std::vector<std::string> lns_operators = {"random", "time", "related", "dual", "rejected"};

    std::string out_dir;             // where solution.json / trace.jsonl / dynamics.csv go
    bool trace = true;               // write trace.jsonl (event stream for the web app)
    bool lp_snapshot = true;         // dump the fractional root LP solution in the trace
};
