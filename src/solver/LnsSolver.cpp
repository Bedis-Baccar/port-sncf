#include "LnsSolver.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <set>
#include <stdexcept>

#include "Interrupt.h"
#include "SolutionWriter.h"

// ============================================================== setup, pool ==
LnsSolver::LnsSolver(const TimeSpaceGraph& graph, const std::vector<Train>& trains, const SolverParams& params)
    : graph_(graph),
      trains_(trains),
      params_(params),
      master_(graph, trains, params.threads,
              (params.out_dir.empty() ? std::string("..") : params.out_dir) + "/gurobi_log.txt"),
      pricing_(graph, trains),
      cols_by_train_(trains.size()),
      best_(trains.size()),
      cur_(trains.size()),
      path_rc_(trains.size(), 0.0),
      visited_nodes_(trains.size()),
      cur_cells_(trains.size()),
      rng_(static_cast<unsigned>(params.lns_seed)) {
    start_ = std::chrono::steady_clock::now();
    slots_ = graph.get_horizon() / graph.get_time_step() + 1;
    // One refusal ("dummy") column per train, always first in cols_by_train_[k].
    for (size_t k = 0; k < trains_.size(); ++k) {
        int a = graph_.get_dummy_arc_id(k);
        Column dummy(static_cast<int>(k), {a}, std::vector<bool>(graph_.get_nb_services(), true),
                     graph_.get_arc(a).get_cost(k), static_cast<int>(k));
        add_column(dummy);
    }
    for (const std::string& name : params_.lns_operators) dops_.push_back(Operator{name});
    if (dops_.empty()) dops_.push_back(Operator{"random"});
    for (const std::string& name : params_.lns_repairs) rops_.push_back(Operator{name});
    if (rops_.empty()) rops_.push_back(Operator{"dive"});
}

double LnsSolver::elapsed() const {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start_).count();
}
bool LnsSolver::time_up() const { return elapsed() >= params_.time_limit_s; }

int LnsSolver::add_column(Column& col) {
    std::vector<int> key = col.arc_ids;
    key.push_back(-1 - col.train_id);
    auto it = column_index_.find(key);
    if (it != column_index_.end()) return it->second;
    int id = pool_.add_column(col);
    column_index_[key] = id;
    if (master_open_) master_.add_column(col, graph_);
    cols_by_train_[col.train_id].push_back(id);
    return id;
}

std::vector<int> LnsSolver::cells_of(int column) const {
    std::vector<int> cells;
    for (int a : pool_.get_column(column).arc_ids)
        for (const auto& [r, t] : graph_.get_arc(a).get_iNodes()) cells.push_back(r * slots_ + t / graph_.get_time_step());
    return cells;
}

double LnsSolver::solution_cost(const std::vector<int>& columns) const {
    double v = 0;
    for (int c : columns) v += pool_.get_column(c).cost;
    return v;
}

std::vector<int> LnsSolver::flatten(const std::vector<std::vector<int>>& per_train) const {
    std::vector<int> out;
    for (const auto& v : per_train) out.insert(out.end(), v.begin(), v.end());
    return out;
}

uint64_t LnsSolver::solution_hash(const std::vector<int>& columns) const {
    std::vector<int> s = columns;
    std::sort(s.begin(), s.end());
    uint64_t h = 1469598103934665603ULL;   // FNV-1a
    for (int c : s) { h ^= static_cast<uint64_t>(c) + 0x9e3779b97f4a7c15ULL; h *= 1099511628211ULL; }
    return h;
}

bool LnsSolver::refused(int train) const {
    return cur_[train].size() == 1 && cur_[train][0] == cols_by_train_[train][0];
}

// ===================================================================== root ==
LnsSolver::CgResult LnsSolver::root_column_generation(double gap) {
    CgResult r;
    const int max_iterations = 400, stall_window = 60;
    double best_rmp = INF;
    int last_improvement = 0;
    for (int it = 0; it < max_iterations; ++it) {
        if (tusp::stop_requested() || time_up()) { r.interrupted = true; return r; }
        auto t0 = std::chrono::steady_clock::now();
        master_.solve();
        if (master_.is_interrupted()) { tusp::request_stop(); r.interrupted = true; return r; }
        if (!master_.is_optimal())
            throw std::runtime_error("LNS root: master not optimal (status " + std::to_string(master_.get_gurobi_status()) + ")");
        const double rmp = master_.get_objective_value();
        root_pi_ = master_.get_flow_duals();
        root_alpha_ = master_.get_service_duals();
        root_mu_ = master_.get_conflict_duals();
        auto t1 = std::chrono::steady_clock::now();
        time_master_ += std::chrono::duration<double>(t1 - t0).count();
        auto [cols, rcs] = pricing_.solve_subset(root_pi_, root_alpha_, root_mu_, nullptr, nullptr);
        time_pricing_ += std::chrono::duration<double>(std::chrono::steady_clock::now() - t1).count();
        const double lagr = rmp + std::accumulate(rcs.begin(), rcs.end(), 0.0);
        r.lb = std::max(r.lb, lagr);
        r.lp = rmp;
        r.iterations = it + 1;
        if (trace_.enabled()) {
            double min_rc = 0.0;
            for (double v : rcs) min_rc = std::min(min_rc, v);
            trace_.write(JsonObj().str("ev", "cg").str("ctx", "root").integer("node", 0).integer("it", it)
                             .num("t", elapsed()).num("rmp", rmp).num("lagr", lagr)
                             .integer("added", cols.size()).integer("pool", pool_.size()).num("min_rc", min_rc));
        }
        if (rmp < best_rmp - 1e-6 * std::max(1.0, std::abs(best_rmp))) { best_rmp = rmp; last_improvement = it; }
        if (cols.empty() || std::abs(rmp - lagr) <= gap * std::max(1.0, std::abs(lagr))) return r;
        if (it - last_improvement >= stall_window || it == max_iterations - 1) { r.stalled = true; return r; }
        const size_t before = pool_.size();
        for (Column& c : cols) add_column(c);
        if (pool_.size() == before) return r;
    }
    return r;
}

bool LnsSolver::root_mip(double time_limit, double cutoff, std::vector<int>& solution, double& value) {
    auto t0 = std::chrono::steady_clock::now();
    master_.convert_to_integer();
    master_.set_time_limit(std::max(0.5, time_limit));
    if (cutoff < INF) master_.set_cutoff(cutoff);
    master_.set_focus_to_finding_sol();
    master_.solve();
    bool found = !master_.is_infeasible() && master_.get_current_nb_solutions() > 0;
    if (found) {
        solution = master_.get_active_columns_ids();
        value = solution_cost(solution);
    }
    master_.set_time_limit(GRB_INFINITY);
    master_.set_cutoff(GRB_INFINITY);
    master_.set_focus_to_auto();
    master_.convert_to_continuous();
    time_mip_ += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return found;
}

// ================================================================= repairs ==
void LnsSolver::fixed_occupation(const std::vector<char>& fixed, std::vector<char>& cells, double& cost) const {
    cells.assign(static_cast<size_t>(graph_.get_nb_conflict_resources()) * slots_, 0);
    cost = 0;
    for (size_t k = 0; k < trains_.size(); ++k) {
        if (!fixed[k]) continue;
        for (int c : cur_[k]) {
            cost += pool_.get_column(c).cost;
            for (int cell : cells_of(c)) cells[cell] = 1;
        }
    }
}

// Inserts the trains of `order` one by one through the pricing: train k is priced with
// pi_k = its refusal cost and mu = -BIG on cells already used, so the DP returns the
// cheapest conflict-free path doing all its services, or nothing (refusal is cheaper).
std::vector<int> LnsSolver::greedy_insert(const std::vector<int>& order, const std::vector<char>& fixed_mask) {
    auto t0 = std::chrono::steady_clock::now();
    const int n = static_cast<int>(trains_.size());
    std::vector<double> pi(n, 0.0);
    std::vector<std::vector<double>> alpha(n, std::vector<double>(graph_.get_nb_services(), 0.0));
    std::vector<std::vector<double>> mu(graph_.get_nb_conflict_resources(), std::vector<double>(slots_, 0.0));
    auto occupy = [&](int col) { for (int cell : cells_of(col)) mu[cell / slots_][cell % slots_] = -BIG; };
    for (int k = 0; k < n; ++k)
        if (fixed_mask[k])
            for (int c : cur_[k]) occupy(c);
    std::vector<int> result;
    std::vector<char> active(n, 0);
    for (int k : order) {
        const int dummy = cols_by_train_[k][0];
        pi[k] = pool_.get_column(dummy).cost;
        active[k] = 1;
        auto [cols, rcs] = pricing_.solve_subset(pi, alpha, mu, nullptr, &active);
        active[k] = 0;
        int chosen = cols.empty() ? dummy : add_column(cols[0]);
        occupy(chosen);
        result.push_back(chosen);
    }
    time_greedy_ += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return result;
}

LnsSolver::Candidate LnsSolver::repair_greedy(const std::vector<int>& freed, const std::vector<char>& fixed) {
    Candidate c;
    const int n = static_cast<int>(trains_.size());
    std::vector<int> rank(n, 0);
    for (int i = 0; i < (int)lp_accept_order_.size(); ++i) rank[lp_accept_order_[i]] = i;
    std::vector<int> first = freed;   // refused trains first, then the LP's preference
    std::stable_sort(first.begin(), first.end(), [&](int x, int y) {
        if (refused(x) != refused(y)) return refused(x);
        return rank[x] < rank[y];
    });
    std::vector<int> second = freed;
    std::shuffle(second.begin(), second.end(), rng_);
    for (const std::vector<int>& order : {first, second}) {
        std::vector<int> sol = greedy_insert(order, fixed);
        for (int b = 0; b < n; ++b)
            if (fixed[b]) sol.insert(sol.end(), cur_[b].begin(), cur_[b].end());
        double val = solution_cost(sol);
        if (!c.found || val < c.value) { c.found = true; c.value = val; c.columns = sol; }
    }
    return c;
}

// Regret insertion: repeatedly insert the train whose refusal would cost the most compared
// with its best conflict-free insertion; only the trains whose best path is hit by the
// insertion are re-priced.
LnsSolver::Candidate LnsSolver::repair_regret(const std::vector<int>& freed, const std::vector<char>& fixed) {
    auto t0 = std::chrono::steady_clock::now();
    Candidate c;
    const int n = static_cast<int>(trains_.size());
    std::vector<double> pi(n, 0.0);
    std::vector<std::vector<double>> alpha(n, std::vector<double>(graph_.get_nb_services(), 0.0));
    std::vector<std::vector<double>> mu(graph_.get_nb_conflict_resources(), std::vector<double>(slots_, 0.0));
    std::vector<char> used(static_cast<size_t>(graph_.get_nb_conflict_resources()) * slots_, 0);
    auto occupy = [&](int col) { for (int cell : cells_of(col)) { mu[cell / slots_][cell % slots_] = -BIG; used[cell] = 1; } };
    for (int k = 0; k < n; ++k)
        if (fixed[k])
            for (int col : cur_[k]) occupy(col);
    std::vector<char> active(n, 0);
    auto price = [&](int k) -> int {   // best conflict-free column of k, or -1
        pi[k] = pool_.get_column(cols_by_train_[k][0]).cost;
        active[k] = 1;
        auto [cols, rcs] = pricing_.solve_subset(pi, alpha, mu, nullptr, &active);
        active[k] = 0;
        return cols.empty() ? -1 : add_column(cols[0]);
    };
    std::vector<int> remaining = freed, best_col(n, -1);
    for (int k : remaining) best_col[k] = price(k);
    std::vector<int> sol;
    std::uniform_real_distribution<double> U(0.0, 1e-6);
    while (!remaining.empty()) {
        int pick = -1;
        double best_regret = -INF;
        for (int k : remaining) {
            if (best_col[k] < 0) continue;
            double regret = pool_.get_column(cols_by_train_[k][0]).cost - pool_.get_column(best_col[k]).cost + U(rng_);
            if (regret > best_regret) { best_regret = regret; pick = k; }
        }
        if (pick < 0) {   // nobody can be inserted any more: refuse the rest
            for (int k : remaining) sol.push_back(cols_by_train_[k][0]);
            break;
        }
        const int col = best_col[pick];
        sol.push_back(col);
        occupy(col);
        remaining.erase(std::find(remaining.begin(), remaining.end(), pick));
        for (int k : remaining) {
            if (best_col[k] < 0) continue;
            bool conflict = false;
            for (int cell : cells_of(best_col[k]))
                if (used[cell]) { conflict = true; break; }
            if (conflict) best_col[k] = price(k);
        }
    }
    for (int b = 0; b < n; ++b)
        if (fixed[b]) sol.insert(sol.end(), cur_[b].begin(), cur_[b].end());
    c.found = true;
    c.columns = sol;
    c.value = solution_cost(sol);
    time_greedy_ += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return c;
}

LnsSolver::CgResult LnsSolver::sub_column_generation(SubMaster& sm, std::vector<char>& priced, double fixed_cost,
                                                     double prune_at, int it) {
    CgResult r;
    const int max_iterations = 300, stall_window = 25;
    double best_rmp = INF;
    int last_improvement = 0;
    std::vector<double> pi;
    std::vector<std::vector<double>> alpha, mu;
    for (int cg = 0; cg < max_iterations; ++cg) {
        if (tusp::stop_requested() || time_up()) { r.interrupted = true; return r; }
        if (cg > 0 && elapsed() >= iter_deadline_) { r.stalled = true; r.over_budget = true; return r; }
        auto t0 = std::chrono::steady_clock::now();
        sm.solve();
        if (sm.interrupted()) { tusp::request_stop(); r.interrupted = true; return r; }
        if (sm.infeasible()) { r.infeasible = true; return r; }
        if (!sm.optimal()) throw std::runtime_error("LNS: sub-master not optimal (status " + std::to_string(sm.status()) + ")");
        const double rmp = fixed_cost + sm.objective();
        sm.duals(pi, alpha, mu, BIG);
        auto t1 = std::chrono::steady_clock::now();
        time_master_ += std::chrono::duration<double>(t1 - t0).count();
        auto [cols, rcs] = pricing_.solve_subset(pi, alpha, mu, nullptr, &priced);
        time_pricing_ += std::chrono::duration<double>(std::chrono::steady_clock::now() - t1).count();
        // fixed trains are constants; trains forced by the dive have one column in the
        // sub-master (rc >= 0): the bound is valid for the restricted problem
        const double lagr = rmp + std::accumulate(rcs.begin(), rcs.end(), 0.0);
        r.lb = std::max(r.lb, lagr);
        r.lp = rmp;
        r.iterations = cg + 1;
        if (trace_.enabled()) {
            double min_rc = 0.0;
            for (double v : rcs) min_rc = std::min(min_rc, v);
            trace_.write(JsonObj().str("ev", "cg").str("ctx", "lns").integer("node", it).integer("it", cg)
                             .num("t", elapsed()).num("rmp", rmp).num("lagr", lagr)
                             .integer("added", cols.size()).integer("pool", pool_.size()).num("min_rc", min_rc));
        }
        if (r.lb >= prune_at) { r.pruned = true; return r; }
        if (rmp < best_rmp - 1e-6 * std::max(1.0, std::abs(best_rmp))) { best_rmp = rmp; last_improvement = cg; }
        if (cols.empty() || std::abs(rmp - lagr) <= params_.lns_cg_gap * std::max(1.0, std::abs(lagr))) return r;
        if (cg - last_improvement >= stall_window || cg == max_iterations - 1) { r.stalled = true; return r; }
        int added = 0;
        for (Column& col : cols) {
            int id = add_column(col);
            if (!sm.has_column(id) && sm.add_column(id, pool_.get_column(id))) added++;
        }
        if (added == 0) return r;
    }
    return r;
}

// Price-and-dive on a reduced sub-master: CG pricing only the freed trains; fix the most
// integral columns and re-price until the LP is integral. Stops as soon as the bound proves
// that nothing better than the current solution (<= cur - 1) exists in the (dive) subproblem.
LnsSolver::Candidate LnsSolver::repair_dive(const std::vector<int>& freed, const std::vector<char>& free, int it,
                                            bool with_mip) {
    Candidate c;
    const int n = static_cast<int>(trains_.size());
    std::vector<char> fixed(n, 1);
    for (int b : freed) fixed[b] = 0;
    std::vector<char> blocked;
    double fixed_cost = 0;
    fixed_occupation(fixed, blocked, fixed_cost);
    SubMaster sm(master_.env(), graph_, trains_, free, blocked, params_.threads);
    const size_t cap = 300;   // most recent columns per freed train (+ refusal + current)
    for (int k : freed) {
        sm.add_column(cols_by_train_[k][0], pool_.get_column(cols_by_train_[k][0]));
        for (int col : cur_[k]) sm.add_column(col, pool_.get_column(col));
        const auto& all = cols_by_train_[k];
        for (size_t i = all.size(), added = 0; i-- > 1 && added < cap;)
            if (sm.add_column(all[i], pool_.get_column(all[i]))) added++;
    }
    const double target = cur_cost_ - 1 + 1e-6;   // integer costs: strictly better than current
    const double prune_at = params_.lns_bound_pruning ? target : INF;
    std::vector<char> priced = free;
    while (true) {
        CgResult cg = sub_column_generation(sm, priced, fixed_cost, prune_at, it);
        if (c.dive_steps == 0) { c.lb = cg.lb; }
        if (cg.over_budget) c.over_budget = true;
        c.cg_it += cg.iterations;
        if (cg.interrupted) { c.interrupted = true; break; }
        if (cg.infeasible) break;
        if (cg.pruned) {
            if (c.dive_steps == 0) c.pruned = true; else c.cut = true;
            break;
        }
        if (cg.over_budget) break;   // iteration budget spent: give up this neighbourhood
        std::vector<std::pair<int, double>> act = sm.active();
        if (sm.integral()) {
            std::vector<int> sol;
            for (auto& [col, x] : act) sol.push_back(col);
            for (int b = 0; b < n; ++b)
                if (fixed[b]) sol.insert(sol.end(), cur_[b].begin(), cur_[b].end());
            c.found = true;
            c.columns = sol;
            c.value = solution_cost(sol);
            break;
        }
        // fix columns >= 0.99 (pairwise conflict-free) or else the largest fractional one,
        // never rounding a refusal column
        std::sort(act.begin(), act.end(), [](auto& a, auto& b) { return a.second > b.second; });
        int fixed_now = 0;
        for (auto& [col, x] : act) {
            const int k = pool_.get_column(col).train_id;
            if (!priced[k]) continue;
            const bool refusal = (col == cols_by_train_[k][0]);
            if (x >= 0.99 || (fixed_now == 0 && !refusal)) {
                sm.keep_only(k, col);
                priced[k] = 0;
                fixed_now++;
            }
            if (x < 0.99 && fixed_now > 0) break;
        }
        if (fixed_now == 0) break;
        c.dive_steps++;
    }
    if (with_mip && !c.pruned && !c.interrupted && !c.over_budget && !time_up() && params_.lns_mip_time_s > 0) {
        // MIP polish on all the columns of the neighbourhood, cutoff: better than current
        // and than the dive's own solution
        auto t0 = std::chrono::steady_clock::now();
        sm.restore_bounds();
        double cutoff = std::min(target, c.found ? c.value - 1 + 1e-6 : INF) - fixed_cost;
        std::vector<int> ids;
        bool timed_out = false;
        double tl = std::min(params_.lns_mip_time_s, std::max(0.5, params_.time_limit_s - elapsed()));
        if (sm.solve_mip(tl, cutoff, ids, timed_out)) {
            for (int b = 0; b < n; ++b)
                if (fixed[b]) ids.insert(ids.end(), cur_[b].begin(), cur_[b].end());
            double val = solution_cost(ids);
            if (!c.found || val < c.value) { c.found = true; c.columns = ids; c.value = val; }
        }
        c.mip_timeout = timed_out;
        time_mip_ += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    }
    c.rows = sm.num_rows();
    c.cols = sm.num_cols();
    dive_steps_ += c.dive_steps;
    return c;
}

// =============================================================== solutions ==
void LnsSolver::compute_profiles() {
    const int ts = graph_.get_time_step();
    for (size_t k = 0; k < trains_.size(); ++k) {
        std::set<int> nodes;
        double rc = 0.0;
        cur_cells_[k].clear();
        for (int c : cur_[k]) {
            const Column& col = pool_.get_column(c);
            double r = col.cost;
            if (!root_pi_.empty()) {
                r -= root_pi_[k];
                for (size_t s = 0; s < col.services.size(); ++s)
                    if (col.services[s] && trains_[k].get_service(static_cast<int>(s))) r -= root_alpha_[k][s];
            }
            for (int a : col.arc_ids) {
                const Arc& arc = graph_.get_arc(a);
                for (int v : {arc.get_from(), arc.get_to()}) {
                    int p = graph_.get_node(v).getPNode();
                    if (p >= 0) nodes.insert(p);
                }
                for (const auto& [res, t] : arc.get_iNodes()) {
                    if (!root_mu_.empty()) r -= root_mu_[res][t / ts];
                    cur_cells_[k].push_back(res * slots_ + t / ts);
                }
            }
            rc += r;
        }
        path_rc_[k] = rc;
        visited_nodes_[k].assign(nodes.begin(), nodes.end());
    }
}

void LnsSolver::set_current(const std::vector<int>& columns, double value) {
    for (auto& v : cur_) v.clear();
    for (int c : columns) cur_[pool_.get_column(c).train_id].push_back(c);
    cur_cost_ = value;
    visited_.insert(solution_hash(columns));
    compute_profiles();
}

void LnsSolver::set_best(const std::vector<int>& columns, double value, const std::string& source) {
    for (auto& v : best_) v.clear();
    for (int c : columns) best_[pool_.get_column(c).train_id].push_back(c);
    ub_ = value;
    if (ttfs_ < 0) ttfs_ = elapsed();
    int refused_count = 0;
    for (size_t k = 0; k < trains_.size(); ++k)
        if (best_[k].size() == 1 && best_[k][0] == cols_by_train_[k][0]) refused_count++;
    std::cout << " * \033[32mNOUVELLE SOLUTION (" << source << ") : " << value << "\033[0m (" << refused_count
              << " trains refuses)\n";
    if (trace_.enabled()) {
        trace_.write(JsonObj().str("ev", "incumbent").num("t", elapsed()).num("value", value).str("source", source)
                         .raw("train_paths", paths_json(columns, false)));
    }
}

std::string LnsSolver::paths_json(const std::vector<int>& column_ids, bool with_flow) const {
    std::string s = "[";
    bool first = true;
    for (int id : column_ids) {
        const Column& col = pool_.get_column(id);
        JsonObj o;
        o.integer("train_id", col.train_id).integer("cost", col.cost);
        if (with_flow) o.num("flow", master_.get_column_value(id));
        o.raw("arc_sequence", JsonObj::int_array(col.arc_ids));
        if (!first) s += ',';
        first = false;
        s += o.dump();
    }
    return s + "]";
}

void LnsSolver::trace_bounds(int it) const {
    if (!trace_.enabled()) return;
    double gap = (ub_ < INF && root_lb_ > 0) ? (ub_ - root_lb_) / root_lb_ * 100.0 : NAN;
    trace_.write(JsonObj().str("ev", "bounds").num("t", elapsed()).integer("it", it).num("lb", root_lb_)
                     .num("ub", ub_).num("gap", gap).integer("pool", pool_.size())
                     .num("t_master", time_master_).num("t_pricing", time_pricing_).num("t_heur", time_mip_));
}

// ================================================================ operators ==
size_t LnsSolver::pick(const std::vector<Operator>& ops) {
    double total = 0;
    for (auto& o : ops) total += o.weight;
    double x = std::uniform_real_distribution<double>(0.0, total)(rng_);
    for (size_t i = 0; i + 1 < ops.size(); ++i) {
        if (x < ops[i].weight) return i;
        x -= ops[i].weight;
    }
    return ops.size() - 1;
}

// Segment update (Ropke & Pisinger): w <- (1 - rho) w + rho * normalised performance.
// Destroy operators: mean score per use. Repair operators: score per second, because the
// repairs differ in cost by orders of magnitude. Normalised by the mean over the operators
// used in the segment, so weights stay O(1); operators not used keep their weight.
void LnsSolver::end_segment() {
    const double rho = params_.lns_reaction;
    auto update = [&](std::vector<Operator>& ops, bool per_second) {
        std::vector<double> perf(ops.size(), -1.0);
        double sum = 0;
        int used = 0;
        for (size_t i = 0; i < ops.size(); ++i) {
            if (!ops[i].seg_uses) continue;
            perf[i] = per_second ? ops[i].seg_score / (ops[i].seg_time + 0.05 * ops[i].seg_uses)
                                 : ops[i].seg_score / ops[i].seg_uses;
            sum += perf[i];
            used++;
        }
        if (used > 0 && sum > 0) {
            const double mean = sum / used;
            for (size_t i = 0; i < ops.size(); ++i)
                if (perf[i] >= 0) ops[i].weight = std::max(0.1, (1 - rho) * ops[i].weight + rho * perf[i] / mean);
        }
        for (auto& o : ops) { o.seg_score = 0; o.seg_time = 0; o.seg_uses = 0; }
    };
    update(dops_, false);
    update(rops_, true);
}

std::vector<int> LnsSolver::destroy(const std::string& op, int k) {
    const int n = static_cast<int>(trains_.size());
    std::uniform_real_distribution<double> U(0.0, 1.0);
    auto overlap = [&](int a, int b) {
        return std::max(0, std::min(trains_[a].get_d_t(), trains_[b].get_d_t()) -
                               std::max(trains_[a].get_a_t(), trains_[b].get_a_t()));
    };
    auto top_k = [&](std::vector<std::pair<double, int>> scored) {
        std::sort(scored.begin(), scored.end(), [](auto& x, auto& y) { return x.first > y.first; });
        std::vector<int> out;
        for (int i = 0; i < k && i < (int)scored.size(); ++i) out.push_back(scored[i].second);
        return out;
    };
    std::vector<int> chosen;

    if (op == "time") {
        int s = std::uniform_int_distribution<int>(0, n - 1)(rng_);
        std::vector<std::pair<double, int>> sc;
        for (int b = 0; b < n; ++b)
            sc.push_back({overlap(s, b) - 1e-3 * std::abs(trains_[b].get_a_t() - trains_[s].get_a_t()) + 1e-6 * U(rng_), b});
        chosen = top_k(sc);
    } else if (op == "related") {
        int s = std::uniform_int_distribution<int>(0, n - 1)(rng_);
        std::vector<std::pair<double, int>> sc;
        for (int b = 0; b < n; ++b) {
            std::vector<int> common;
            std::set_intersection(visited_nodes_[s].begin(), visited_nodes_[s].end(), visited_nodes_[b].begin(),
                                  visited_nodes_[b].end(), std::back_inserter(common));
            double score = (b == s) ? 1e18 : (common.size() + 1.0) * (overlap(s, b) + 1.0) + 1e-3 * U(rng_);
            sc.push_back({score, b});
        }
        chosen = top_k(sc);
    } else if (op == "bottleneck" && !bottleneck_cells_.empty()) {
        // a cell (resource, time) that the root LP prices as most contested (most negative
        // conflict dual), chosen at random proportionally to -mu; free the trains that use
        // the resource around that time and the trains (refused ones first) present then
        double total = 0;
        for (auto& [w, cell] : bottleneck_cells_) total += w;
        double x = U(rng_) * total;
        int cell = bottleneck_cells_.back().second;
        for (auto& [w, cl] : bottleneck_cells_) { if (x < w) { cell = cl; break; } x -= w; }
        const int r = cell / slots_, slot = cell % slots_, window = 6;
        const int t = slot * graph_.get_time_step();
        std::vector<std::pair<double, int>> sc;
        for (int b = 0; b < n; ++b) {
            double score = 1e-3 * U(rng_);
            for (int c2 : cur_cells_[b])
                if (c2 / slots_ == r && std::abs(c2 % slots_ - slot) <= window) { score += 3.0; break; }
            const bool present = trains_[b].get_a_t() <= t && t <= trains_[b].get_d_t();
            if (present) score += refused(b) ? 2.0 : 1.0;
            sc.push_back({score, b});
        }
        chosen = top_k(sc);
    } else if (op == "rejected" || op == "dual") {
        std::vector<int> ref;
        for (int b = 0; b < n; ++b)
            if (refused(b)) ref.push_back(b);
        if (op == "rejected" && !ref.empty()) {
            std::shuffle(ref.begin(), ref.end(), rng_);
            int keep = std::max(1, std::min<int>(ref.size(), k / 2));
            chosen.assign(ref.begin(), ref.begin() + keep);
            int s = chosen[0];
            std::vector<std::pair<double, int>> sc;
            for (int b = 0; b < n; ++b)
                if (std::find(chosen.begin(), chosen.end(), b) == chosen.end())
                    sc.push_back({overlap(s, b) + 1e-6 * U(rng_), b});
            std::sort(sc.begin(), sc.end(), [](auto& x, auto& y) { return x.first > y.first; });
            for (auto& [v, b] : sc) {
                if ((int)chosen.size() >= k) break;
                chosen.push_back(b);
            }
        } else if (op == "dual") {
            std::vector<int> order(n);
            std::iota(order.begin(), order.end(), 0);
            std::sort(order.begin(), order.end(), [&](int a, int b) { return path_rc_[a] > path_rc_[b]; });
            while ((int)chosen.size() < k && !order.empty()) {
                int idx = static_cast<int>(std::pow(U(rng_), 4.0) * order.size());
                chosen.push_back(order[idx]);
                order.erase(order.begin() + idx);
            }
        }
    }
    if (chosen.empty()) {   // "random" and fallbacks
        std::vector<int> all(n);
        std::iota(all.begin(), all.end(), 0);
        std::shuffle(all.begin(), all.end(), rng_);
        chosen.assign(all.begin(), all.begin() + k);
    }
    return chosen;
}

// =================================================================== solve ==
SolveStatistics LnsSolver::solve(const std::string& instance_name) {
    start_ = std::chrono::steady_clock::now();
    const int n = static_cast<int>(trains_.size());
    stats_ = SolveStatistics{};
    stats_.instance_name = instance_name;
    stats_.method = "lns";
    stats_.num_trains = n;
    stats_.num_arcs = graph_.get_arcs().size();
    const bool sa = params_.lns_accept == "sa";

    const std::string out_dir = params_.out_dir.empty() ? std::string("../results/runs/") + instance_name : params_.out_dir;
    if (params_.trace) {
        trace_.open(out_dir + "/trace.jsonl");
        auto list = [](const std::vector<Operator>& ops) {
            std::string s = "[";
            for (size_t i = 0; i < ops.size(); ++i) s += (i ? ",\"" : "\"") + ops[i].name + "\"";
            return s + "]";
        };
        trace_.write(JsonObj().str("ev", "start").str("instance", instance_name).str("method", "lns")
            .integer("trains", n).integer("arcs", graph_.get_arcs().size())
            .integer("T", graph_.get_horizon()).integer("t_s", graph_.get_time_step())
            .raw("params", JsonObj().num("time_limit_s", params_.time_limit_s).str("method", "lns")
                .boolean("link_conflicts", params_.link_conflicts).num("root_cg_gap", params_.cg_gaps[0])
                .num("lns_init_frac", params_.lns_init_frac).num("lns_max_frac", params_.lns_max_frac)
                .num("lns_mip_time_s", params_.lns_mip_time_s).num("lns_cg_gap", params_.lns_cg_gap)
                .num("lns_root_mip_time_s", params_.lns_root_mip_time_s).num("lns_root_time_frac", params_.lns_root_time_frac)
                .integer("lns_seed", params_.lns_seed).boolean("lns_bound_pruning", params_.lns_bound_pruning)
                .integer("lns_max_iter", params_.lns_max_iter).raw("lns_operators", list(dops_)).raw("lns_repairs", list(rops_))
                .str("lns_accept", params_.lns_accept).num("lns_sa_start", params_.lns_sa_start).num("lns_sa_end", params_.lns_sa_end)
                .integer("lns_segment", params_.lns_segment).num("lns_reaction", params_.lns_reaction)
                .num("lns_iter_time_frac", params_.lns_iter_time_frac)
                .integer("threads", params_.threads).dump()));
    }
    std::cout << "\n=== ALNS : " << instance_name << " (" << n << " trains) ===\n";

    // ---------------- first solution: multi-start greedy insertion through the pricing --------
    {
        std::vector<int> base(n);
        std::iota(base.begin(), base.end(), 0);
        auto by = [&](auto key) {
            std::vector<int> o = base;
            std::stable_sort(o.begin(), o.end(), [&](int a, int b) { return key(a) < key(b); });
            return o;
        };
        std::vector<std::pair<std::string, std::vector<int>>> orders = {
            {"arrival", by([&](int k) { return trains_[k].get_a_t(); })},
            {"departure", by([&](int k) { return trains_[k].get_d_t(); })},
            {"short-stay", by([&](int k) { return trains_[k].get_d_t() - trains_[k].get_a_t(); })},
            {"few-services", by([&](int k) { return trains_[k].get_nb_services_required(); })},
        };
        for (int r = 0; r < 4; ++r) {
            std::vector<int> o = base;
            std::shuffle(o.begin(), o.end(), rng_);
            orders.push_back({"random", o});
        }
        const double budget = std::max(0.5, 0.05 * params_.time_limit_s);
        for (auto& [name, order] : orders) {
            if (elapsed() > budget && ub_ < INF) break;
            std::vector<int> sol = greedy_insert(order, std::vector<char>(n, 0));
            double val = solution_cost(sol);
            if (val < ub_ - 0.5) { set_best(sol, val, "greedy:" + name); set_current(sol, val); }
        }
    }

    // ---------------- root: LP bound (time-budgeted) on the global master ----------------
    const double full_limit = params_.time_limit_s;
    params_.time_limit_s = std::min(full_limit, std::max(1.0, params_.lns_root_time_frac * full_limit));
    CgResult root = root_column_generation(params_.cg_gaps[0]);
    params_.time_limit_s = full_limit;
    const bool root_complete = !root.interrupted;
    if (root.interrupted && !tusp::stop_requested()) {
        root.interrupted = false;
        std::cout << "Racine interrompue (budget de temps) : borne partielle valide\n";
    }
    root_lb_ = root.lb;
    stats_.root_lb = root_lb_;
    std::cout << "Racine : LP " << root.lp << ", borne " << root_lb_ << " (" << root.iterations
              << " iterations de CG, " << pool_.size() << " colonnes)\n";
    if (root_complete && trace_.enabled()) {
        trace_.write(JsonObj().str("ev", "lp").integer("node", 0).num("t", elapsed()).num("value", root.lp)
                         .raw("train_paths", paths_json(master_.get_active_columns_ids(), true)));
    }
    if (!root.interrupted) {
        // LP-guided greedy: insert first the trains the root LP most wants to accept
        lp_accept_order_.resize(n);
        std::iota(lp_accept_order_.begin(), lp_accept_order_.end(), 0);
        std::vector<double> refused_flow(n, 1.0);
        for (int k = 0; k < n; ++k) refused_flow[k] = master_.get_column_value(cols_by_train_[k][0]);
        std::stable_sort(lp_accept_order_.begin(), lp_accept_order_.end(),
                         [&](int a, int b) { return refused_flow[a] < refused_flow[b]; });
        std::vector<int> sol = greedy_insert(lp_accept_order_, std::vector<char>(n, 0));
        double val = solution_cost(sol);
        if (val < ub_ - 0.5) { set_best(sol, val, "greedy:lp-guided"); set_current(sol, val); }
        // contested cells for the bottleneck operator
        for (size_t r = 0; r < root_mu_.size(); ++r)
            for (int sl = 0; sl < slots_; ++sl)
                if (root_mu_[r][sl] < -1e-6) bottleneck_cells_.push_back({-root_mu_[r][sl], static_cast<int>(r) * slots_ + sl});
        std::sort(bottleneck_cells_.rbegin(), bottleneck_cells_.rend());
        if (bottleneck_cells_.size() > 200) bottleneck_cells_.resize(200);
    }
    if (!root.interrupted && !time_up() && params_.lns_root_mip_time_s > 0) {
        std::vector<int> sol;
        double val = 0;
        // at most 20 % of the time limit, so that short runs keep time for the ALNS loop
        double tl = std::min({params_.lns_root_mip_time_s, std::max(1.0, 0.2 * params_.time_limit_s),
                              std::max(1.0, params_.time_limit_s - elapsed())});
        if (root_mip(tl, ub_ - 1 + 1e-6, sol, val) && val < ub_ - 0.5) { set_best(sol, val, "root-mip"); set_current(sol, val); }
    }
    master_open_ = false;   // from now on the neighbourhoods use their own sub-masters
    compute_profiles();
    trace_bounds(0);

    // ---------------- ALNS iterations ----------------
    const double eps = 1e-6;
    auto proven_optimal = [&]() { return ub_ <= std::ceil(root_lb_ - eps) + eps; };
    double frac = std::clamp(params_.lns_init_frac, 0.0, 1.0);
    int it = 0, no_best = 0, pruned_count = 0;
    std::string status = "TIME_LIMIT";
    // temperature, relative to the CURRENT cost: a solution p (as a fraction of the current
    // cost) worse is accepted with probability 1/2, p decreasing geometrically from
    // lns_sa_start to lns_sa_end along the time budget
    const double t_loop = elapsed();
    const double p0 = std::max(1e-9, params_.lns_sa_start), p_end = std::max(1e-9, params_.lns_sa_end);
    std::uniform_real_distribution<double> U(1e-12, 1.0);

    std::cout << std::left << std::setw(6) << "It" << std::setw(8) << "Temps" << std::setw(11) << "Destroy"
              << std::setw(9) << "Repair" << std::setw(4) << "k" << std::setw(11) << "Borne" << std::setw(10) << "Cand."
              << std::setw(10) << "Courante" << std::setw(10) << "Meilleure" << "Resultat\n";

    while (!root.interrupted) {
        if (tusp::stop_requested()) { status = "INTERRUPTED"; break; }
        if (time_up()) { status = "TIME_LIMIT"; break; }
        if (proven_optimal()) { status = "OPTIMAL"; break; }
        if (params_.lns_max_iter > 0 && it >= params_.lns_max_iter) { status = "ITERATION_LIMIT"; break; }

        const double progress = params_.lns_max_iter > 0
            ? static_cast<double>(it) / params_.lns_max_iter
            : std::clamp((elapsed() - t_loop) / std::max(1e-9, params_.time_limit_s - t_loop), 0.0, 1.0);
        const double temperature = sa ? -(p0 * std::pow(p_end / p0, progress)) * cur_cost_ / std::log(0.5) : 0.0;
        // acceptance threshold drawn BEFORE the repair (simulated annealing)
        const double threshold = sa ? cur_cost_ - temperature * std::log(U(rng_)) : cur_cost_ - 1;
        const double accept_limit = std::floor(threshold + eps);

        const size_t di = pick(dops_), ri = pick(rops_);
        Operator& dop = dops_[di];
        Operator& rop = rops_[ri];
        const int k = std::clamp(static_cast<int>(std::lround(frac * n)), std::min(2, n), n);
        const double t_start = elapsed();
        iter_deadline_ = t_start + std::max(1.0, params_.lns_iter_time_frac * params_.time_limit_s);
        std::vector<int> freed = destroy(dop.name, k);
        std::vector<char> free(n, 0), fixed(n, 1);
        for (int b : freed) { free[b] = 1; fixed[b] = 0; }

        Candidate cand;
        if (rop.name == "greedy") cand = repair_greedy(freed, fixed);
        else if (rop.name == "regret") cand = repair_regret(freed, fixed);
        else cand = repair_dive(freed, free, it, rop.name == "dive_mip");
        if (cand.interrupted) { status = tusp::stop_requested() ? "INTERRUPTED" : "TIME_LIMIT"; break; }

        std::string result;
        double score = 0;
        const double before = cur_cost_;
        if (cand.pruned) result = "pruned";
        else if (!cand.found) result = "failed";
        else if (solution_hash(cand.columns) == solution_hash(flatten(cur_))) result = "same";
        else if (cand.value < ub_ - 0.5) {
            result = "new_best"; score = 33;   // scores: new best 33, better than current 9, accepted new worse 1
            set_best(cand.columns, cand.value, "lns:" + dop.name + "+" + rop.name);
            set_current(cand.columns, cand.value);
        } else if (cand.value < cur_cost_ - 0.5) {
            result = "improved"; score = 9;
            set_current(cand.columns, cand.value);
        } else if (cand.value <= accept_limit) {
            const bool fresh = !visited_.count(solution_hash(cand.columns));
            result = "accepted"; score = fresh ? 1 : 0;
            set_current(cand.columns, cand.value);
        } else {
            result = "rejected";
        }
        const double dur = elapsed() - t_start;
        for (Operator* o : {&dop, &rop}) {
            o->tries++;
            o->time += dur;
            o->seg_score += score;
            o->seg_time += dur;
            o->seg_uses++;
            if (result == "new_best") o->new_best++;
            else if (result == "improved") o->improved++;
            else if (result == "accepted") o->accepted++;
            else if (result == "pruned") o->pruned++;
        }
        if (result == "pruned") pruned_count++;
        if ((it + 1) % std::max(1, params_.lns_segment) == 0) end_segment();

        // neighbourhood size: grow when stuck or pruned, shrink when the MIP times out,
        // restart small after a long plateau at the maximum
        if (result == "new_best") no_best = 0;
        else {
            no_best++;
            if (result == "pruned") frac *= 1.1;
            else if (cand.mip_timeout || cand.over_budget) frac *= 0.85;
            else if (no_best % 5 == 0) frac *= 1.1;
            if (no_best >= 15 && frac >= params_.lns_max_frac - 1e-9) { frac = params_.lns_init_frac; no_best = 0; }
        }
        frac = std::clamp(frac, std::min(1.0, 2.0 / std::max(1, n)), std::max(params_.lns_init_frac, params_.lns_max_frac));

        std::cout << std::left << std::setw(6) << it << std::setw(8) << std::fixed << std::setprecision(1) << elapsed()
                  << std::setw(11) << dop.name << std::setw(9) << rop.name << std::setw(4) << k << std::setw(11)
                  << std::setprecision(0) << (std::isnan(cand.lb) ? 0.0 : cand.lb) << std::setw(10)
                  << (cand.found ? cand.value : 0.0) << std::setw(10) << cur_cost_ << std::setw(10) << ub_ << result << "\n";
        if (trace_.enabled()) {
            JsonObj w, rw;
            for (auto& o : dops_) w.num(o.name.c_str(), o.weight);
            for (auto& o : rops_) rw.num(o.name.c_str(), o.weight);
            trace_.write(JsonObj().str("ev", "lns").integer("it", it).num("t", elapsed()).num("dur", dur)
                             .str("op", dop.name).str("rep", rop.name).integer("k", k).num("frac", frac)
                             .num("lb", cand.lb).num("value", cand.found ? cand.value : NAN)
                             .num("prev", before).num("cur", cur_cost_).num("ub", ub_)
                             .num("temp", temperature).num("thr", sa ? threshold : NAN).str("result", result)
                             .integer("cg_it", cand.cg_it).integer("dive_steps", cand.dive_steps)
                             .integer("rows", cand.rows).integer("cols", cand.cols)
                             .raw("weights", w.dump()).raw("rweights", rw.dump()));
        }
        it++;
        trace_bounds(it);
    }
    if (root.interrupted) status = tusp::stop_requested() ? "INTERRUPTED" : "TIME_LIMIT";
    if (proven_optimal()) status = "OPTIMAL";

    stats_.status = status;
    stats_.total_time_s = elapsed();
    stats_.time_to_first_sol_s = ttfs_;
    stats_.final_lb = root_lb_;
    stats_.final_ub = ub_;
    stats_.gap_pct = (root_lb_ > 0 && ub_ < INF) ? std::max(0.0, (ub_ - root_lb_) / root_lb_ * 100.0) : 0.0;
    stats_.explored_nodes = it;
    stats_.pruned_nodes = pruned_count;
    stats_.num_columns = pool_.size();
    stats_.time_in_master_s = time_master_;
    stats_.time_in_pricing_s = time_pricing_;
    stats_.time_in_heuristics_s = time_mip_;

    auto stats_json = [](const std::vector<Operator>& ops) {
        std::string s = "[";
        for (size_t i = 0; i < ops.size(); ++i) {
            if (i) s += ",";
            s += JsonObj().str("name", ops[i].name).integer("tries", ops[i].tries).integer("new_best", ops[i].new_best)
                     .integer("improved", ops[i].improved).integer("accepted", ops[i].accepted)
                     .integer("pruned", ops[i].pruned).num("time", ops[i].time).num("weight", ops[i].weight).dump();
        }
        return s + "]";
    };
    if (trace_.enabled()) {
        trace_.write(JsonObj().str("ev", "end").str("method", "lns").num("t", stats_.total_time_s).str("status", status)
                         .num("root_lb", root_lb_).num("lb", root_lb_).num("ub", ub_).num("gap", stats_.gap_pct)
                         .num("ttfs", ttfs_).integer("nodes", it).integer("pruned", pruned_count)
                         .integer("columns", pool_.size()).num("t_master", time_master_).num("t_pricing", time_pricing_)
                         .num("t_heur", time_mip_).num("t_greedy", time_greedy_)
                         .raw("operators", stats_json(dops_)).raw("repairs", stats_json(rops_)));
        trace_.close();
    }
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "\n=======================================================\n";
    std::cout << " FIN ALNS : " << instance_name << " (" << status << ")\n";
    std::cout << "   ├─ Temps total     : " << stats_.total_time_s << " s (master " << time_master_ << ", pricing "
              << time_pricing_ << ", MIP " << time_mip_ << ", insertion " << time_greedy_ << ")\n";
    std::cout << "   ├─ Iterations      : " << it << " (" << pruned_count << " voisinages elagues par la borne, "
              << dive_steps_ << " fixations de plongee)\n";
    std::cout << "   ├─ Borne (racine)  : " << root_lb_ << "\n";
    std::cout << "   ├─ Meilleure sol.  : " << ub_ << "\n";
    std::cout << "   └─ Gap             : " << stats_.gap_pct << " %\n";
    for (auto* ops : {&dops_, &rops_})
        for (auto& o : *ops)
            std::cout << "      " << std::setw(11) << o.name << " essais " << std::setw(4) << o.tries << " meilleure "
                      << std::setw(3) << o.new_best << " amelio. " << std::setw(3) << o.improved << " acceptees "
                      << std::setw(3) << o.accepted << " elagues " << std::setw(3) << o.pruned << " temps "
                      << std::setprecision(1) << o.time << " s poids " << std::setprecision(2) << o.weight << "\n";
    std::cout << "=======================================================\n\n";
    return stats_;
}

void LnsSolver::export_best_solution(const std::string& filename) const {
    std::vector<Column> sol;
    for (const auto& cols : best_)
        for (int c : cols) sol.push_back(pool_.get_column(c));
    write_solution_json(filename, graph_, sol, ub_, stats_, params_.link_conflicts);
}
