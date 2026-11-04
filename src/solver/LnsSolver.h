#pragma once

#include <chrono>
#include <map>
#include <random>
#include <string>
#include <vector>

#include "Column.h"
#include "MasterProblem.h"
#include "PricingProblem.h"
#include "SolveStatistics.h"
#include "SolverParams.h"
#include "TimeSpaceGraph.h"
#include "Trace.h"
#include "Train.h"

/*
Large Neighbourhood Search built on the column-generation machinery of the B&P.

  root     : column generation over all trains -> LP bound (valid global lower bound)
             + MIP on the generated columns -> first solution (all trains refused if none).
  iterate  : destroy  = choose k trains (adaptive operator, adaptive size), the other
                        trains are fixed to their current columns;
             repair   = column generation pricing ONLY the freed trains, then a MIP on the
                        restricted master with cutoff UB - 1 (costs are integers).
  bounds   : the neighbourhood's Lagrangian bound z_RMP + sum_k min(0, rc_k) is valid for
             the restricted problem (fixed trains have a single column, already in the RMP),
             so a neighbourhood whose bound >= UB - 1 cannot contain a better solution:
             its CG stops and its MIP is skipped. UB = ceil(root bound) proves optimality.
*/
class LnsSolver {
public:
    LnsSolver(const TimeSpaceGraph& graph, const std::vector<Train>& trains, const SolverParams& params);
    SolveStatistics solve(const std::string& instance_name);
    void export_best_solution(const std::string& filename) const;

private:
    struct Operator {
        std::string name;
        double weight = 1.0;
        int tries = 0, improvements = 0, pruned = 0;
    };
    struct CgResult {
        double lp = 0.0;          // last RMP value
        double lb = -1e18;        // best Lagrangian bound of the (restricted) problem
        int iterations = 0;
        bool pruned = false;      // lb >= cutoff: cannot improve
        bool interrupted = false;
        bool infeasible = false;
        bool stalled = false;     // RMP value stuck (degeneracy): stopped early
    };
    struct DiveResult {
        CgResult first;           // CG of the neighbourhood itself (its bound)
        bool interrupted = false;
        bool pruned_at_root = false;  // the neighbourhood bound proves it cannot improve
        bool cut = false;         // a dive step was cut by its bound
        bool found = false;
        std::vector<int> solution;
        double value = 0;
        int steps = 0;
    };
    DiveResult dive_repair(std::vector<char> free, int it);

    int add_column(Column& col);
    // enable / disable columns: free trains keep all their columns, forced trains one,
    // the others their incumbent columns
    void set_free(const std::vector<char>& free, const std::vector<int>* forced = nullptr);
    CgResult column_generation(const std::vector<char>* free, double gap, const char* ctx, int ctx_id, double prune_at);
    bool solve_mip(double time_limit, double cutoff, std::vector<int>& solution, double& value);
    // Greedy insertion through the pricing oracle: trains of `order` are inserted one by
    // one, each priced with pi_k = cost of refusing it and mu = -BIG on the resources
    // already used, so the pricing returns the cheapest conflict-free path doing all its
    // services (or nothing: the train is refused). Returns one column id per train.
    std::vector<int> greedy_insert(const std::vector<int>& order, const std::vector<char>& fixed_mask);
    double solution_cost(const std::vector<int>& columns) const;
    void set_incumbent(const std::vector<int>& columns, double value, const std::string& source);
    std::vector<int> destroy(const std::string& op, int k);
    void compute_train_profiles();
    double elapsed() const;
    bool time_up() const;
    std::string paths_json(const std::vector<int>& column_ids, bool with_flow) const;
    void trace_bounds(int it) const;

    const TimeSpaceGraph& graph_;
    const std::vector<Train>& trains_;
    SolverParams params_;
    MasterProblem master_;
    PricingProblem pricing_;
    ColumnPool pool_;

    std::vector<std::vector<int>> cols_by_train_;
    std::map<std::vector<int>, int> column_index_;   // arc sequence (+train) -> column id (no duplicates)
    double time_greedy_ = 0;
    int greedy_improvements_ = 0;
    long long dive_steps_ = 0;
    std::vector<int> lp_accept_order_;           // trains sorted by root LP "acceptance"
    std::vector<char> enabled_;                  // per column
    std::vector<std::vector<int>> incumbent_;    // columns of the incumbent, per train
    double ub_ = 1e18;
    double root_lb_ = -1e18;
    double root_lp_ = 0.0;

    // root duals (dual-guided destroy) and per-train profiles of the incumbent
    std::vector<double> root_pi_;
    std::vector<std::vector<double>> root_alpha_, root_mu_;
    std::vector<double> path_rc_;                // reduced cost of the incumbent path, root duals
    std::vector<std::vector<int>> visited_nodes_;// physical nodes used by the incumbent path

    std::vector<Operator> ops_;
    std::mt19937 rng_;
    std::chrono::steady_clock::time_point start_;
    mutable Trace trace_;
    double time_master_ = 0, time_pricing_ = 0, time_mip_ = 0;
    double ttfs_ = -1;
    SolveStatistics stats_{};
    constexpr static double INF = 1e18;
};
