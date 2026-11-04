#pragma once

#include <atomic>
#include <chrono>
#include <fstream>
#include <string>
#include <tuple>
#include <vector>

#include "TimeSpaceGraph.h"
#include "Train.h"
#include "MasterProblem.h"
#include "PricingProblem.h"
#include "GlobalStateManager.h"
#include "SearchTree.h"
#include "BPNode.h"
#include "SolverParams.h"
#include "Trace.h"
#include "SolveStatistics.h"
#include "Interrupt.h"


struct BranchingDecision {
    std::vector<int> left_train_ids;
    std::vector<int> right_train_ids;
    std::map<int,std::vector<int>> left_forbidden_arcs;
    std::map<int, std::vector<int>> right_forbidden_arcs;
    // Description for the trace / web app
    std::string rule;          // "service_split" or "quantum"
    std::string left_label;
    std::string right_label;
    int train = -1;            // branched train
    int service = -1;          // service_split: service id
    int split_time = -1;       // service_split: left = start <= split_time, right = start > split_time
    int divergence_time = -1;  // quantum: time of the divergence node
};

enum class SolutionStatus{
    OPTIMAL,
    NOT_FOUND,
    FEASIBLE,
    UNFEASIBLE
};

enum class SolveStatus{
    OPTIMAL,
    TIME_LIMIT_REACHED,
    GAP_TOLERANCE_REACHED,
    INTERRUPTED
};

class BranchAndPriceSolver {
public:
    BranchAndPriceSolver(const TimeSpaceGraph& graph, const std::vector<Train>& trains,
                         const SolverParams& params = SolverParams());
    SolveStatistics solve(double time_limit_s, const std::string& instance_name);
    SolveStatistics solve(const std::string& instance_name) { return solve(params_.time_limit_s, instance_name); }
    double get_optimal_value() const;
    std::vector<Column> get_optimal_solution() const;
    void export_best_solution(const std::string& filename) const;

    // Asynchronous stop request (signal handler safe): the solver finishes the
    // current step, then stops and still exports its best solution.
    static void request_stop() { tusp::request_stop(); }
    static bool stop_requested() { return tusp::stop_requested(); }

private:
    void run_column_generation(BPNode* node, double termination_gap, const char* context = "node");
    bool is_current_solution_integer() const;
    BranchingDecision choose_branching_variable() const;
    BranchingDecision quantum_train_branching() const;
    BranchingDecision service_split_branching() const;
    std::vector<BPNode*> branch_on_node(BPNode* parent, const BranchingDecision& decision);
    int add_Column(Column& col);
    void switch_state(BPNode* target);
    BPNode* next_node();

    void run_global_mip_heuristic(double time_limit_seconds);
    void run_diving_heuristic(BPNode base_node);
    void update_incumbent(double value, const std::vector<int>& column_ids, const char* source);

    // Trace helpers
    double elapsed() const;
    bool time_up() const { return elapsed() >= time_limit_s_; }
    double time_limit_s_ = 1e18;
    std::string paths_json(const std::vector<int>& column_ids, bool with_flow) const;
    void trace_node(const BPNode* node, const char* event) const;

    void print_log_header() const;
    void print_log_line(int iter, int depth, double elapsed_s, double lb, double ub, const std::string& action) const;


    const TimeSpaceGraph& graph_;
    const std::vector<Train>& trains_;
    SolverParams params_;
    MasterProblem master_;
    PricingProblem pricing_;
    GlobalStateManager state_manager_;
    SearchTree tree_;
    ColumnPool column_pool_;

    double time_master_ = 0.0;
    double time_pricing_ = 0.0;
    double time_heuristics_ = 0.0;

    std::chrono::steady_clock::time_point start_time_;
    mutable Trace trace_;
    SolveStatistics last_stats_{};
public:
    long long checked_columns_ = 0;   // debug: columns whose reduced cost was cross-checked
private:

    constexpr static double INF = 1e+9;
};
