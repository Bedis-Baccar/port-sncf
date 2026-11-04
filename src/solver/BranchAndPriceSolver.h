#pragma once

#include <vector>
#include <tuple>

#include "TimeSpaceGraph.h"
#include "Train.h"
#include "MasterProblem.h"
#include "PricingProblem.h"
#include "GlobalStateManager.h"
#include "SearchTree.h"
#include "BPNode.h"


struct BranchingDecision {
    std::vector<int> left_train_ids;
    std::vector<int> right_train_ids;
    std::map<int,std::vector<int>> left_forbidden_arcs;  
    std::map<int, std::vector<int>> right_forbidden_arcs; 
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

// Structure pour stocker les résultats d'une instance
struct SolveStatistics {
    std::string instance_name;
    int num_trains;
    int num_arcs;
    
    double total_time_s;
    double time_to_first_sol_s; // -1 si aucune solution trouvée
    
    double root_lb;
    double final_lb;
    double final_ub;
    double gap_pct;
    
    int explored_nodes;
    int pruned_nodes;
    
    // Profiling
    double time_in_master_s;
    double time_in_pricing_s;
    double time_in_heuristics_s;
};

class BranchAndPriceSolver {
public:
    BranchAndPriceSolver(const TimeSpaceGraph& graph, const std::vector<Train>& trains);
    SolveStatistics solve(double time_limit_s, const std::string& instance_name);
    double get_optimal_value() const;
    std::vector<Column> get_optimal_solution() const;
    void export_best_solution(const std::string& filename) const;
private:
    void run_column_generation(BPNode* node, double termination_gap);
    bool is_current_solution_integer() const;
    BranchingDecision choose_branching_variable() const;
    BranchingDecision quantum_train_branching() const;
    BranchingDecision service_split_branching() const;
    void branch_on_node(BPNode* parent, const BranchingDecision& decision);
    int add_Column(Column& col);
    void switch_state(BPNode* target);

    void run_global_mip_heuristic(double time_limit_seconds);
    void run_diving_heuristic(BPNode base_node);

    void print_log_header() const;
    void print_log_line(int iter, int depth, double elapsed_s, double lb, double ub, const std::string& action) const;


    const TimeSpaceGraph& graph_;
    const std::vector<Train>& trains_;
    MasterProblem master_;
    PricingProblem pricing_;
    GlobalStateManager state_manager_;
    SearchTree tree_;
    ColumnPool column_pool_;

    double time_master_ = 0.0;
    double time_pricing_ = 0.0;
    double time_heuristics_ = 0.0;

    constexpr static double INF = 1e+9;
};