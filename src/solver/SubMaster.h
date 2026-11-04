#pragma once

#include <unordered_map>
#include <vector>

#include "Column.h"
#include "TimeSpaceGraph.h"
#include "Train.h"
#include "gurobi_c++.h"

/*
Restricted master of ONE LNS neighbourhood.

Only the freed trains are in the model. The trains kept fixed are not variables:
the (resource, time) cells they occupy are "blocked" — columns using them are not
added, and the pricing sees mu = -BIG on them, so it never generates such paths.
Conflict rows are created lazily, the first time a column of the neighbourhood uses
the cell: a cell used by no column has a slack row whose dual is 0.

The model is small (k trains, their columns, the cells they actually touch) and is
rebuilt for every neighbourhood on the Gurobi environment of the global master.
*/
class SubMaster {
public:
    SubMaster(GRBEnv& env, const TimeSpaceGraph& graph, const std::vector<Train>& trains,
              const std::vector<char>& free, const std::vector<char>& blocked, int threads);

    // Adds a column of a freed train; returns false if it uses a blocked cell.
    bool add_column(int pool_id, const Column& col);
    bool has_column(int pool_id) const { return local_of_.count(pool_id) > 0; }

    // Dive: train k keeps only column pool_id (the other columns get UB = 0).
    void keep_only(int train, int pool_id);
    void restore_bounds();

    void solve();
    bool optimal() const;
    bool infeasible() const;
    bool interrupted() const;
    int status() const;
    double objective() const;

    // Duals in the dense layout expected by the pricing (fixed trains: 0; blocked cells: -big).
    void duals(std::vector<double>& pi, std::vector<std::vector<double>>& alpha,
               std::vector<std::vector<double>>& mu, double big) const;

    std::vector<std::pair<int, double>> active() const;   // (pool id, value > 1e-6)
    bool integral() const;

    // MIP on the columns of the neighbourhood; true if a solution with objective <= cutoff exists.
    bool solve_mip(double time_limit, double cutoff, std::vector<int>& pool_ids, bool& timed_out);

    int num_rows() const { return rows_; }
    int num_cols() const { return static_cast<int>(vars_.size()); }

private:
    const TimeSpaceGraph& graph_;
    const std::vector<Train>& trains_;
    mutable GRBModel model_;   // mutable: Gurobi getters are not const in every version
    std::vector<char> free_;
    const std::vector<char>& blocked_;   // flat [resource * slots + slot]
    int slots_;
    int ts_;
    int rows_ = 0;

    mutable std::vector<GRBConstr> flow_;                 // per train (free trains only)
    mutable std::vector<std::vector<GRBConstr>> svc_;     // per train, per required service
    mutable std::unordered_map<int, GRBConstr> conflict_; // cell -> row, created on first use
    mutable std::vector<GRBVar> vars_;
    std::vector<int> var_pool_, var_train_;
    std::unordered_map<int, int> local_of_;       // pool id -> local index
};
