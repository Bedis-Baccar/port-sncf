#include "SubMaster.h"

#include <string>

SubMaster::SubMaster(GRBEnv& env, const TimeSpaceGraph& graph, const std::vector<Train>& trains,
                     const std::vector<char>& free, const std::vector<char>& blocked, int threads)
    : graph_(graph),
      trains_(trains),
      model_(env),
      free_(free),
      blocked_(blocked),
      slots_(graph.get_horizon() / graph.get_time_step() + 1),
      ts_(graph.get_time_step()),
      flow_(trains.size()),
      svc_(trains.size()) {
    model_.set(GRB_IntParam_OutputFlag, 0);
    if (threads > 0) model_.set(GRB_IntParam_Threads, threads);
    model_.set(GRB_IntAttr_ModelSense, GRB_MINIMIZE);
    const int ns = graph.get_nb_services();
    for (size_t k = 0; k < trains.size(); ++k) {
        if (!free_[k]) continue;
        flow_[k] = model_.addConstr(0.0, GRB_GREATER_EQUAL, 1.0);
        rows_++;
        svc_[k].resize(ns);
        for (int s = 0; s < ns; ++s) {
            if (trains[k].get_service(s)) {
                svc_[k][s] = model_.addConstr(0.0, GRB_GREATER_EQUAL, 1.0);
                rows_++;
            }
        }
    }
}

bool SubMaster::add_column(int pool_id, const Column& col) {
    if (local_of_.count(pool_id)) return true;
    const int k = col.train_id;
    if (!free_[k]) return false;
    // occupation of the column, cell by cell (a column may use a cell several times only
    // in theory; the coefficient then follows the global master: one term per occurrence)
    std::vector<int> cells;
    for (int a : col.arc_ids) {
        for (const auto& [r, t] : graph_.get_arc(a).get_iNodes()) {
            const int cell = r * slots_ + t / ts_;
            if (blocked_[cell]) return false;
            cells.push_back(cell);
        }
    }
    GRBColumn gc;
    gc.addTerm(1.0, flow_[k]);
    for (size_t s = 0; s < col.services.size(); ++s)
        if (col.services[s] && trains_[k].get_service(static_cast<int>(s))) gc.addTerm(1.0, svc_[k][s]);
    for (int cell : cells) {
        auto it = conflict_.find(cell);
        if (it == conflict_.end()) {
            it = conflict_.emplace(cell, model_.addConstr(0.0, GRB_LESS_EQUAL, 1.0)).first;
            rows_++;
        }
        gc.addTerm(1.0, it->second);
    }
    GRBVar v = model_.addVar(0.0, GRB_INFINITY, col.cost, GRB_CONTINUOUS, gc);
    local_of_[pool_id] = static_cast<int>(vars_.size());
    vars_.push_back(v);
    var_pool_.push_back(pool_id);
    var_train_.push_back(k);
    return true;
}

void SubMaster::keep_only(int train, int pool_id) {
    for (size_t i = 0; i < vars_.size(); ++i)
        if (var_train_[i] == train && var_pool_[i] != pool_id) vars_[i].set(GRB_DoubleAttr_UB, 0.0);
}

void SubMaster::restore_bounds() {
    for (auto& v : vars_) v.set(GRB_DoubleAttr_UB, GRB_INFINITY);
}

void SubMaster::solve() { model_.optimize(); }
int SubMaster::status() const { return model_.get(GRB_IntAttr_Status); }
bool SubMaster::optimal() const { return status() == GRB_OPTIMAL; }
bool SubMaster::infeasible() const { return status() == GRB_INFEASIBLE; }
bool SubMaster::interrupted() const { return status() == GRB_INTERRUPTED; }
double SubMaster::objective() const { return model_.get(GRB_DoubleAttr_ObjVal); }

void SubMaster::duals(std::vector<double>& pi, std::vector<std::vector<double>>& alpha,
                      std::vector<std::vector<double>>& mu, double big) const {
    const int n = static_cast<int>(trains_.size());
    const int ns = graph_.get_nb_services();
    const int R = graph_.get_nb_conflict_resources();
    pi.assign(n, 0.0);
    alpha.assign(n, std::vector<double>(ns, 0.0));
    mu.assign(R, std::vector<double>(slots_, 0.0));
    for (int k = 0; k < n; ++k) {
        if (!free_[k]) continue;
        pi[k] = flow_[k].get(GRB_DoubleAttr_Pi);
        for (int s = 0; s < ns; ++s)
            if (trains_[k].get_service(s)) alpha[k][s] = svc_[k][s].get(GRB_DoubleAttr_Pi);
    }
    for (int r = 0; r < R; ++r)
        for (int sl = 0; sl < slots_; ++sl)
            if (blocked_[r * slots_ + sl]) mu[r][sl] = -big;
    for (auto& [cell, row] : conflict_) mu[cell / slots_][cell % slots_] = row.get(GRB_DoubleAttr_Pi);
}

std::vector<std::pair<int, double>> SubMaster::active() const {
    std::vector<std::pair<int, double>> out;
    for (size_t i = 0; i < vars_.size(); ++i) {
        double x = vars_[i].get(GRB_DoubleAttr_X);
        if (x > 1e-6) out.push_back({var_pool_[i], x});
    }
    return out;
}

bool SubMaster::integral() const {
    for (auto& v : vars_) {
        double x = v.get(GRB_DoubleAttr_X);
        if (x > 1e-5 && x < 1.0 - 1e-5) return false;
    }
    return true;
}

bool SubMaster::solve_mip(double time_limit, double cutoff, std::vector<int>& pool_ids, bool& timed_out) {
    for (auto& v : vars_) v.set(GRB_CharAttr_VType, GRB_BINARY);
    model_.set(GRB_DoubleParam_TimeLimit, time_limit);
    model_.set(GRB_DoubleParam_Cutoff, cutoff);
    model_.set(GRB_IntParam_MIPFocus, 1);
    model_.optimize();
    timed_out = status() == GRB_TIME_LIMIT;
    pool_ids.clear();
    if (model_.get(GRB_IntAttr_SolCount) == 0) return false;
    for (size_t i = 0; i < vars_.size(); ++i)
        if (vars_[i].get(GRB_DoubleAttr_X) > 0.5) pool_ids.push_back(var_pool_[i]);
    return true;
}
