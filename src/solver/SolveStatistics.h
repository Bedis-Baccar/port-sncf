#pragma once
#include <string>

// Results of one solve (Branch-and-Price or LNS)
struct SolveStatistics {
    std::string instance_name;
    std::string method = "bp";  // bp or lns
    std::string status;         // OPTIMAL, TIME_LIMIT, INTERRUPTED, INFEASIBLE
    int num_trains = 0;
    int num_arcs = 0;

    double total_time_s = 0;
    double time_to_first_sol_s = -1; // -1 si aucune solution trouvée

    double root_lb = -1e9;
    double final_lb = -1e9;
    double final_ub = 1e9;
    double gap_pct = 0;

    int explored_nodes = 0;          // B&P nodes, or LNS iterations
    int pruned_nodes = 0;            // B&P pruned nodes, or LNS neighbourhoods pruned by bound
    int num_columns = 0;

    // Profiling
    double time_in_master_s = 0;
    double time_in_pricing_s = 0;
    double time_in_heuristics_s = 0; // B&P: primal heuristics; LNS: MIP repairs
};
