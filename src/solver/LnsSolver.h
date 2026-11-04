#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <unordered_set>
#include <vector>

#include "Column.h"
#include "MasterProblem.h"
#include "PricingProblem.h"
#include "SolveStatistics.h"
#include "SolverParams.h"
#include "SubMaster.h"
#include "TimeSpaceGraph.h"
#include "Trace.h"
#include "Train.h"

/*
Adaptive Large Neighbourhood Search built on the column-generation machinery of the B&P.

  root      greedy insertion through the pricing oracle (several orders)  -> first solution
            column generation on all trains (time budget)              -> LP bound (valid)
            LP-guided greedy insertion + MIP on the root columns
  iterate   destroy : free k trains of the CURRENT solution (6 operators, adaptive weights)
            repair  : one of 4 operators (adaptive weights normalised by time):
                        greedy   - insertion through the pricing, refused trains first
                        regret   - insertion by largest regret (refusal cost - best insertion)
                        dive     - price-and-dive on a reduced sub-master
                        dive_mip - price-and-dive, then a MIP on the sub-master's columns
            accept  : simulated annealing (threshold drawn before the repair) or improvement only
  bounds    sub-master Lagrangian bound z + sum_k min(0, rc_k) (+ cost of the fixed trains) is valid
            for the neighbourhood; LP repairs stop as soon as it proves that no solution
            better than the current one (integer costs: <= cur - 1) exists there.
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
        int tries = 0, new_best = 0, improved = 0, accepted = 0, pruned = 0;
        double time = 0.0;
        // current segment (weights are updated every `lns_segment` iterations)
        double seg_score = 0.0, seg_time = 0.0;
        int seg_uses = 0;
    };
    struct CgResult {
        double lp = 0.0;          // last RMP value (+ cost of the fixed trains)
        double lb = -1e18;        // best Lagrangian bound of the (restricted) problem
        int iterations = 0;
        bool pruned = false;      // lb >= prune threshold
        bool interrupted = false;
        bool infeasible = false;
        bool stalled = false;
        bool over_budget = false; // iteration time budget spent
    };
    struct Candidate {
        bool found = false, pruned = false, cut = false, interrupted = false, mip_timeout = false, over_budget = false;
        std::vector<int> columns;  // full solution (all trains)
        double value = 0.0;
        double lb = NAN;           // neighbourhood bound (LP repairs)
        int cg_it = 0, dive_steps = 0, rows = 0, cols = 0;
    };

    // pool & root (global master)
    int add_column(Column& col);
    CgResult root_column_generation(double gap);
    bool root_mip(double time_limit, double cutoff, std::vector<int>& solution, double& value);

    // repairs
    std::vector<int> greedy_insert(const std::vector<int>& order, const std::vector<char>& fixed_mask);
    Candidate repair_greedy(const std::vector<int>& freed, const std::vector<char>& fixed);
    Candidate repair_regret(const std::vector<int>& freed, const std::vector<char>& fixed);
    Candidate repair_dive(const std::vector<int>& freed, const std::vector<char>& free, int it, bool with_mip);
    CgResult sub_column_generation(SubMaster& sm, std::vector<char>& priced, double fixed_cost, double prune_at, int it);
    void fixed_occupation(const std::vector<char>& fixed, std::vector<char>& cells, double& cost) const;
    std::vector<int> cells_of(int column) const;

    // solutions
    double solution_cost(const std::vector<int>& columns) const;
    std::vector<int> flatten(const std::vector<std::vector<int>>& per_train) const;
    uint64_t solution_hash(const std::vector<int>& columns) const;
    void set_current(const std::vector<int>& columns, double value);
    void set_best(const std::vector<int>& columns, double value, const std::string& source);
    bool refused(int train) const;
    void compute_profiles();

    // operators
    std::vector<int> destroy(const std::string& op, int k);
    size_t pick(const std::vector<Operator>& ops);
    void end_segment();

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
    bool master_open_ = true;                   // columns also go to the global master (root phase)

    std::vector<std::vector<int>> cols_by_train_;
    std::map<std::vector<int>, int> column_index_;

    std::vector<std::vector<int>> best_, cur_;  // solution columns per train
    double ub_ = 1e18, cur_cost_ = 1e18;
    double root_lb_ = -1e18;
    std::unordered_set<uint64_t> visited_;

    // root duals (dual-guided and bottleneck operators), profiles of the current solution
    std::vector<double> root_pi_;
    std::vector<std::vector<double>> root_alpha_, root_mu_;
    std::vector<std::pair<double, int>> bottleneck_cells_;   // (-mu, cell), most contested first
    std::vector<double> path_rc_;
    std::vector<std::vector<int>> visited_nodes_, cur_cells_;
    std::vector<int> lp_accept_order_;

    std::vector<Operator> dops_, rops_;
    std::mt19937 rng_;
    std::chrono::steady_clock::time_point start_;
    mutable Trace trace_;
    double time_master_ = 0, time_pricing_ = 0, time_mip_ = 0, time_greedy_ = 0;
    double ttfs_ = -1;
    long long dive_steps_ = 0;
    SolveStatistics stats_{};
    int slots_ = 0;
    double iter_deadline_ = 1e18;                 // soft deadline of the current iteration
    constexpr static double INF = 1e18;
    constexpr static double BIG = 1e7;            // > any refusal cost
};
