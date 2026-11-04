#include "LnsSolver.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>

#include "Interrupt.h"
#include "SolutionWriter.h"

LnsSolver::LnsSolver(const TimeSpaceGraph& graph, const std::vector<Train>& trains, const SolverParams& params)
    : graph_(graph),
      trains_(trains),
      params_(params),
      master_(graph, trains, params.threads,
              (params.out_dir.empty() ? std::string("..") : params.out_dir) + "/gurobi_log.txt"),
      pricing_(graph, trains),
      cols_by_train_(trains.size()),
      incumbent_(trains.size()),
      path_rc_(trains.size(), 0.0),
      visited_nodes_(trains.size()),
      rng_(static_cast<unsigned>(params.lns_seed)) {
    start_ = std::chrono::steady_clock::now();
    // One dummy column ("refused train") per train: the master is always feasible.
    for (size_t k = 0; k < trains_.size(); ++k) {
        int a = graph_.get_dummy_arc_id(k);
        Column dummy(static_cast<int>(k), {a}, std::vector<bool>(graph_.get_nb_services(), true),
                     graph_.get_arc(a).get_cost(k), static_cast<int>(k));
        add_column(dummy);
    }
    for (const std::string& name : params_.lns_operators) {
        ops_.push_back(Operator{name});
    }
    if (ops_.empty()) ops_.push_back(Operator{"random"});
}

double LnsSolver::elapsed() const {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start_).count();
}

bool LnsSolver::time_up() const { return elapsed() >= params_.time_limit_s; }

int LnsSolver::add_column(Column& col) {
    std::vector<int> key = col.arc_ids;
    key.push_back(-1 - col.train_id);
    auto it = column_index_.find(key);
    if (it != column_index_.end()) return it->second;   // already in the pool
    int id = pool_.add_column(col);
    column_index_[key] = id;
    master_.add_column(col, graph_);
    cols_by_train_[col.train_id].push_back(id);
    enabled_.push_back(1);
    return id;
}

void LnsSolver::set_free(const std::vector<char>& free, const std::vector<int>* forced) {
    std::vector<char> in_incumbent(pool_.size(), 0);
    for (const auto& cols : incumbent_)
        for (int c : cols) in_incumbent[c] = 1;
    std::vector<int> to_enable, to_disable;
    for (size_t k = 0; k < trains_.size(); ++k) {
        const int f = forced ? (*forced)[k] : -1;
        for (int c : cols_by_train_[k]) {
            char want = f >= 0 ? (c == f) : free[k] ? 1 : in_incumbent[c];
            if (want != enabled_[c]) {
                (want ? to_enable : to_disable).push_back(c);
                enabled_[c] = want;
            }
        }
    }
    master_.enable_columns(to_enable);
    master_.disable_columns(to_disable);
}

LnsSolver::CgResult LnsSolver::column_generation(const std::vector<char>* free, double gap, const char* ctx,
                                                 int ctx_id, double prune_at) {
    CgResult r;
    const int max_iterations = 400;
    // Degenerate restricted masters can add columns for a long time without moving the
    // RMP value: stop when it has not improved for `stall_window` iterations (the
    // Lagrangian bound computed so far remains valid).
    const int stall_window = (free == nullptr) ? 60 : 25;
    double best_rmp = INF;
    int last_improvement = 0;
    for (int it = 0; it < max_iterations; ++it) {
        if (tusp::stop_requested() || time_up()) {
            r.interrupted = true;
            return r;
        }
        auto t0 = std::chrono::steady_clock::now();
        master_.solve();
        if (master_.is_interrupted()) {
            tusp::request_stop();
            r.interrupted = true;
            return r;
        }
        if (master_.is_infeasible()) {   // cannot happen with dummy columns; be safe in dives
            r.infeasible = true;
            return r;
        }
        if (!master_.is_optimal()) {
            throw std::runtime_error("LNS: restricted master not optimal (Gurobi status " +
                                     std::to_string(master_.get_gurobi_status()) + ")");
        }
        const double rmp = master_.get_objective_value();
        const std::vector<double> pi = master_.get_flow_duals();
        const std::vector<std::vector<double>> alpha = master_.get_service_duals();
        const std::vector<std::vector<double>> mu = master_.get_conflict_duals();
        auto t1 = std::chrono::steady_clock::now();
        time_master_ += std::chrono::duration<double>(t1 - t0).count();

        auto [cols, rcs] = pricing_.solve_subset(pi, alpha, mu, nullptr, free);
        time_pricing_ += std::chrono::duration<double>(std::chrono::steady_clock::now() - t1).count();

        // Fixed trains have a single column, already in the RMP: their best reduced
        // cost is >= 0 and contributes nothing, so this is a valid bound of the
        // restricted problem (the full problem when free == nullptr).
        const double lagr = rmp + std::accumulate(rcs.begin(), rcs.end(), 0.0);
        r.lb = std::max(r.lb, lagr);
        r.lp = rmp;
        r.iterations = it + 1;
        if (free == nullptr) {  // root: keep the duals for the dual-guided destroy operator
            root_pi_ = pi;
            root_alpha_ = alpha;
            root_mu_ = mu;
        }
        if (trace_.enabled()) {
            double min_rc = 0.0;
            for (double v : rcs) min_rc = std::min(min_rc, v);
            trace_.write(JsonObj().str("ev", "cg").str("ctx", ctx).integer("node", ctx_id).integer("it", it)
                             .num("t", elapsed()).num("rmp", rmp).num("lagr", lagr)
                             .integer("added", cols.size()).integer("pool", pool_.size()).num("min_rc", min_rc));
        }
        if (r.lb >= prune_at) {
            r.pruned = true;
            return r;
        }
        if (rmp < best_rmp - 1e-6 * std::max(1.0, std::abs(best_rmp))) {
            best_rmp = rmp;
            last_improvement = it;
        }
        // every return happens right after a solve: the master's solution stays readable
        if (cols.empty() || std::abs(rmp - lagr) <= gap * std::max(1.0, std::abs(lagr))) {
            return r;
        }
        if (it - last_improvement >= stall_window || it == max_iterations - 1) {
            r.stalled = true;
            return r;
        }
        const size_t before = pool_.size();
        for (Column& c : cols) add_column(c);
        if (pool_.size() == before) return r;   // only duplicates: nothing new to price in
    }
    return r;
}

LnsSolver::DiveResult LnsSolver::dive_repair(std::vector<char> free, int it) {
    DiveResult d;
    const int n = static_cast<int>(trains_.size());
    const double improve_below = ub_ - 1 + 1e-6;   // integer costs: improving means <= UB - 1
    std::vector<int> forced(n, -1);
    while (true) {
        set_free(free, &forced);
        CgResult cg = column_generation(&free, params_.lns_cg_gap, "lns", it,
                                        params_.lns_bound_pruning ? improve_below : INF);
        if (d.steps == 0) d.first = cg;
        if (cg.interrupted) { d.interrupted = true; return d; }
        if (cg.infeasible) return d;
        if (cg.pruned) {                      // bound >= UB - 1: this (dive) subproblem cannot improve
            if (d.steps == 0) d.pruned_at_root = true; else d.cut = true;
            return d;
        }
        std::vector<int> active = master_.get_active_columns_ids();
        if (master_.is_sol_integer()) {
            double val = solution_cost(active);
            if (val <= improve_below) { d.found = true; d.solution = active; d.value = val; }
            return d;
        }
        // fix the columns that are (almost) integral; if none, the largest fractional one.
        // Two columns >= 0.99 never conflict (conflict rows: their sum <= 1), so this
        // keeps the restricted master feasible.
        // A refusal ("dummy") column is never fixed by rounding a fractional value:
        // refusing a train is the last resort and fixing it usually kills the dive.
        std::vector<std::pair<double, int>> cand;   // (value, column) of free, non-fixed trains
        for (int c : active) {
            int k = pool_.get_column(c).train_id;
            if (free[k]) cand.push_back({master_.get_column_value(c), c});
        }
        if (cand.empty()) return d;
        std::sort(cand.rbegin(), cand.rend());
        int fixed_now = 0;
        for (auto& [v, c] : cand) {
            int k = pool_.get_column(c).train_id;
            if (!free[k]) continue;
            const bool refusal = (c == cols_by_train_[k][0]);
            if (v >= 0.99 || (fixed_now == 0 && !refusal)) {
                forced[k] = c;
                free[k] = 0;
                fixed_now++;
            }
            if (v < 0.99 && fixed_now > 0) break;
        }
        if (fixed_now == 0) return d;   // only fractional refusals left: stop the dive
        d.steps++;
    }
}

// true when the MIP found a solution better than the cutoff
bool LnsSolver::solve_mip(double time_limit, double cutoff, std::vector<int>& solution, double& value) {
    auto t0 = std::chrono::steady_clock::now();
    master_.convert_to_integer();
    master_.set_time_limit(std::max(0.5, time_limit));
    if (cutoff < INF) master_.set_cutoff(cutoff);
    master_.set_focus_to_finding_sol();
    master_.solve();
    bool found = !master_.is_infeasible() && master_.get_current_nb_solutions() > 0;
    if (found) {
        solution = master_.get_active_columns_ids();
        value = 0.0;
        for (int c : solution) value += pool_.get_column(c).cost;
    }
    master_.set_time_limit(GRB_INFINITY);
    master_.set_cutoff(GRB_INFINITY);
    master_.set_focus_to_auto();
    master_.convert_to_continuous();
    time_mip_ += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return found;
}

double LnsSolver::solution_cost(const std::vector<int>& columns) const {
    double v = 0;
    for (int c : columns) v += pool_.get_column(c).cost;
    return v;
}

std::vector<int> LnsSolver::greedy_insert(const std::vector<int>& order, const std::vector<char>& fixed_mask) {
    auto t0 = std::chrono::steady_clock::now();
    const int n = static_cast<int>(trains_.size());
    const int slots = graph_.get_horizon() / graph_.get_time_step() + 1;
    const double BIG = 1e7;   // larger than any refusal cost: a conflicting path is never chosen
    std::vector<double> pi(n, 0.0);
    std::vector<std::vector<double>> alpha(n, std::vector<double>(graph_.get_nb_services(), 0.0));
    std::vector<std::vector<double>> mu(graph_.get_nb_conflict_resources(), std::vector<double>(slots, 0.0));
    auto occupy = [&](int col) {
        for (int a : pool_.get_column(col).arc_ids)
            for (const auto& [r, t] : graph_.get_arc(a).get_iNodes()) mu[r][t / graph_.get_time_step()] = -BIG;
    };
    for (int k = 0; k < n; ++k)
        if (fixed_mask[k])
            for (int c : incumbent_[k]) occupy(c);

    std::vector<int> result;
    std::vector<char> active(n, 0);
    for (int k : order) {
        const int dummy = cols_by_train_[k][0];
        pi[k] = pool_.get_column(dummy).cost;    // reduced cost < 0  <=>  cheaper than refusing
        active[k] = 1;
        auto [cols, rcs] = pricing_.solve_subset(pi, alpha, mu, nullptr, &active);
        active[k] = 0;
        int chosen = dummy;
        if (!cols.empty()) chosen = add_column(cols[0]);
        occupy(chosen);
        result.push_back(chosen);
    }
    time_greedy_ += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return result;
}

void LnsSolver::compute_train_profiles() {
    for (size_t k = 0; k < trains_.size(); ++k) {
        std::set<int> nodes;
        double rc = 0.0;
        for (int c : incumbent_[k]) {
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
                if (!root_mu_.empty())
                    for (const auto& [res, t] : arc.get_iNodes()) r -= root_mu_[res][t / graph_.get_time_step()];
            }
            rc += r;
        }
        path_rc_[k] = rc;
        visited_nodes_[k].assign(nodes.begin(), nodes.end());
    }
}

void LnsSolver::set_incumbent(const std::vector<int>& columns, double value, const std::string& source) {
    for (auto& v : incumbent_) v.clear();
    for (int c : columns) incumbent_[pool_.get_column(c).train_id].push_back(c);
    ub_ = value;
    if (ttfs_ < 0) ttfs_ = elapsed();
    compute_train_profiles();
    int refused = 0;
    for (size_t k = 0; k < trains_.size(); ++k)
        if (incumbent_[k].size() == 1 && pool_.get_column(incumbent_[k][0]).arc_ids.size() == 1) refused++;
    std::cout << " * \033[32mNOUVELLE SOLUTION (" << source << ") : " << value << "\033[0m (" << refused
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
        // trains present in the station at the same time as a random seed train
        int s = std::uniform_int_distribution<int>(0, n - 1)(rng_);
        std::vector<std::pair<double, int>> sc;
        for (int b = 0; b < n; ++b)
            sc.push_back({overlap(s, b) - 1e-3 * std::abs(trains_[b].get_a_t() - trains_[s].get_a_t()) + 1e-6 * U(rng_), b});
        chosen = top_k(sc);
    } else if (op == "related") {
        // trains sharing physical nodes with a seed train while present at the same time
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
    } else if (op == "dual" || op == "rejected") {
        std::vector<int> refused;
        for (int b = 0; b < n; ++b)
            if (incumbent_[b].size() == 1 && pool_.get_column(incumbent_[b][0]).arc_ids.size() == 1) refused.push_back(b);
        if (op == "rejected" && !refused.empty()) {
            // refused trains + the trains competing with them for the same time window
            std::shuffle(refused.begin(), refused.end(), rng_);
            int keep = std::max(1, std::min<int>(refused.size(), k / 2));
            chosen.assign(refused.begin(), refused.begin() + keep);
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
        } else {
            // dual-guided: paths that the LP relaxation disagrees with the most
            // (large reduced cost under the root duals), randomised greedy selection
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
    if (chosen.empty()) {  // "random" (and fallback)
        std::vector<int> all(n);
        std::iota(all.begin(), all.end(), 0);
        std::shuffle(all.begin(), all.end(), rng_);
        chosen.assign(all.begin(), all.begin() + k);
    }
    return chosen;
}

SolveStatistics LnsSolver::solve(const std::string& instance_name) {
    start_ = std::chrono::steady_clock::now();
    const int n = static_cast<int>(trains_.size());
    stats_ = SolveStatistics{};
    stats_.instance_name = instance_name;
    stats_.method = "lns";
    stats_.num_trains = n;
    stats_.num_arcs = graph_.get_arcs().size();

    const std::string out_dir = params_.out_dir.empty() ? std::string("../results/runs/") + instance_name : params_.out_dir;
    if (params_.trace) {
        trace_.open(out_dir + "/trace.jsonl");
        std::string ops = "[";
        for (size_t i = 0; i < ops_.size(); ++i) ops += (i ? ",\"" : "\"") + ops_[i].name + "\"";
        ops += "]";
        trace_.write(JsonObj().str("ev", "start").str("instance", instance_name).str("method", "lns")
            .integer("trains", n).integer("arcs", graph_.get_arcs().size())
            .integer("T", graph_.get_horizon()).integer("t_s", graph_.get_time_step())
            .raw("params", JsonObj().num("time_limit_s", params_.time_limit_s).str("method", "lns")
                .boolean("link_conflicts", params_.link_conflicts).num("root_cg_gap", params_.cg_gaps[0])
                .num("lns_init_frac", params_.lns_init_frac).num("lns_max_frac", params_.lns_max_frac)
                .num("lns_mip_time_s", params_.lns_mip_time_s)
                .num("lns_cg_gap", params_.lns_cg_gap).num("lns_root_mip_time_s", params_.lns_root_mip_time_s)
                .num("lns_root_time_frac", params_.lns_root_time_frac)
                .integer("lns_seed", params_.lns_seed).boolean("lns_bound_pruning", params_.lns_bound_pruning)
                .integer("lns_max_iter", params_.lns_max_iter).raw("lns_operators", ops)
                .integer("threads", params_.threads).dump()));
    }

    std::cout << "\n=== LNS : " << instance_name << " (" << n << " trains) ===\n";

    // ---------------- first solution: multi-start greedy insertion through the pricing ----------------
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
        const double greedy_budget = std::max(0.5, 0.05 * params_.time_limit_s);
        for (auto& [name, order] : orders) {
            if (elapsed() > greedy_budget && ub_ < INF) break;
            std::vector<int> sol = greedy_insert(order, std::vector<char>(n, 0));
            double val = solution_cost(sol);
            if (val < ub_ - 0.5) set_incumbent(sol, val, "greedy:" + name);
        }
    }

    // ---------------- root: LP bound (time-budgeted) ----------------
    const double full_limit = params_.time_limit_s;
    params_.time_limit_s = std::min(full_limit, std::max(1.0, params_.lns_root_time_frac * full_limit));
    CgResult root = column_generation(nullptr, params_.cg_gaps[0], "root", 0, INF);
    params_.time_limit_s = full_limit;
    const bool root_complete = !root.interrupted;
    if (root.interrupted && !tusp::stop_requested()) {
        root.interrupted = false;   // budget reached: the best Lagrangian bound is still valid
        std::cout << "Racine interrompue (budget de temps) : borne partielle valide\n";
    }
    root_lb_ = root.lb;
    root_lp_ = root.lp;
    stats_.root_lb = root_lb_;
    std::cout << "Racine : LP " << root.lp << ", borne " << root_lb_ << " (" << root.iterations
              << " iterations de CG, " << pool_.size() << " colonnes)\n";
    if (root_complete && trace_.enabled()) {
        trace_.write(JsonObj().str("ev", "lp").integer("node", 0).num("t", elapsed()).num("value", root.lp)
                         .raw("train_paths", paths_json(master_.get_active_columns_ids(), true)));
        trace_.write(JsonObj().str("ev", "node").integer("id", 0).integer("parent", -1).integer("depth", 0)
                         .str("status", "FRACTIONAL").num("lb", root_lb_).str("label", "root").num("t", elapsed()));
    }

    // LP-guided greedy: insert first the trains that the root LP most wants to accept
    // (smallest flow on their "refused" column).
    if (!root.interrupted) {
        lp_accept_order_.resize(n);
        std::iota(lp_accept_order_.begin(), lp_accept_order_.end(), 0);
        std::vector<double> refused_flow(n, 1.0);
        for (int k = 0; k < n; ++k) refused_flow[k] = master_.get_column_value(cols_by_train_[k][0]);
        std::stable_sort(lp_accept_order_.begin(), lp_accept_order_.end(),
                         [&](int a, int b) { return refused_flow[a] < refused_flow[b]; });
        std::vector<int> sol = greedy_insert(lp_accept_order_, std::vector<char>(n, 0));
        double val = solution_cost(sol);
        if (val < ub_ - 0.5) set_incumbent(sol, val, "greedy:lp-guided");
    }

    // MIP on the root columns (cutoff: must beat the greedy solutions)
    if (!root.interrupted && !time_up()) {
        std::vector<int> sol;
        double val = 0;
        double tl = std::min(params_.lns_root_mip_time_s, std::max(1.0, params_.time_limit_s - elapsed()));
        if (solve_mip(tl, ub_ - 1 + 1e-6, sol, val) && val < ub_ - 0.5) set_incumbent(sol, val, "root-mip");
    }
    trace_bounds(0);

    // ---------------- LNS iterations ----------------
    const double eps = 1e-6;
    auto proven_optimal = [&]() { return ub_ <= std::ceil(root_lb_ - eps) + eps; };
    double frac = std::clamp(params_.lns_init_frac, 0.0, 1.0);
    int it = 0, no_improve = 0, pruned_count = 0;
    std::uniform_real_distribution<double> U(0.0, 1.0);
    std::string status = "TIME_LIMIT";

    std::cout << std::left << std::setw(6) << "It" << std::setw(10) << "Temps" << std::setw(10) << "Op"
              << std::setw(5) << "k" << std::setw(14) << "Borne voisin." << std::setw(12) << "UB" << "Resultat\n";

    while (!root.interrupted) {
        if (tusp::stop_requested()) { status = "INTERRUPTED"; break; }
        if (time_up()) { status = "TIME_LIMIT"; break; }
        if (proven_optimal()) { status = "OPTIMAL"; break; }
        if (params_.lns_max_iter > 0 && it >= params_.lns_max_iter) { status = "ITERATION_LIMIT"; break; }

        // roulette wheel on the adaptive weights
        double total = 0;
        for (auto& o : ops_) total += o.weight;
        double pick = U(rng_) * total;
        size_t oi = 0;
        for (; oi + 1 < ops_.size(); ++oi) {
            if (pick < ops_[oi].weight) break;
            pick -= ops_[oi].weight;
        }
        Operator& op = ops_[oi];
        const int k = std::clamp(static_cast<int>(std::lround(frac * n)), std::min(2, n), n);

        const double t_start = elapsed();
        std::vector<int> freed = destroy(op.name, k);
        std::vector<char> free(n, 0);
        for (int b : freed) free[b] = 1;
        set_free(free);

        // 1) fast repair: greedy re-insertion of the freed trains through the pricing.
        //    Two orders: currently refused trains first (they pick their resources before
        //    their competitors are re-inserted around them) then LP order; and random.
        std::string result;
        double new_value = NAN;
        bool greedy_improved = false;
        {
            std::vector<char> fixed(n, 1);
            for (int b : freed) fixed[b] = 0;
            std::vector<int> rank(n, 0);
            for (int i = 0; i < (int)lp_accept_order_.size(); ++i) rank[lp_accept_order_[i]] = i;
            auto refused = [&](int b) {
                return incumbent_[b].size() == 1 && incumbent_[b][0] == cols_by_train_[b][0];
            };
            std::vector<int> first = freed;
            std::stable_sort(first.begin(), first.end(), [&](int x, int y) {
                if (refused(x) != refused(y)) return refused(x);
                return rank[x] < rank[y];
            });
            std::vector<int> second = freed;
            std::shuffle(second.begin(), second.end(), rng_);
            std::vector<int> best_sol;
            double best_val = ub_;
            for (const std::vector<int>& order : {first, second}) {
                std::vector<int> sol = greedy_insert(order, fixed);
                for (int b = 0; b < n; ++b)
                    if (fixed[b]) sol.insert(sol.end(), incumbent_[b].begin(), incumbent_[b].end());
                double val = solution_cost(sol);
                if (val < best_val - 0.5) { best_val = val; best_sol = sol; }
            }
            if (!best_sol.empty()) {
                set_incumbent(best_sol, best_val, "lns:" + op.name + "+greedy");
                greedy_improved = true;
                greedy_improvements_++;
                new_value = best_val;
            }
        }

        // 2) price-and-dive on the neighbourhood: CG (pricing the freed trains only), stop as
        //    soon as the Lagrangian bound proves the (dive) subproblem cannot beat UB - 1,
        //    otherwise fix the most integral columns and re-price, until the LP is integral.
        op.tries++;
        DiveResult dive = dive_repair(free, it);
        if (dive.interrupted) { status = tusp::stop_requested() ? "INTERRUPTED" : "TIME_LIMIT"; break; }
        const CgResult& cg = dive.first;
        if (dive.found) {
            new_value = dive.value;
            set_incumbent(dive.solution, dive.value, "lns:" + op.name);
            result = "improved";
        } else if (dive.pruned_at_root) {
            result = "pruned";
            op.pruned++;
            pruned_count++;
        } else if (params_.lns_mip_time_s > 0 && !time_up()) {
            // 3) optional polish: MIP on the neighbourhood's columns, cutoff UB - 1
            set_free(free);
            std::vector<int> sol;
            double val = 0;
            double tl = std::min(params_.lns_mip_time_s, std::max(0.5, params_.time_limit_s - elapsed()));
            bool found = solve_mip(tl, ub_ - 1 + eps, sol, val);
            if (found && val <= ub_ - 1 + eps) {
                new_value = val;
                set_incumbent(sol, val, "lns:" + op.name + "+mip");
                result = "improved";
            } else {
                result = master_.get_gurobi_status() == GRB_TIME_LIMIT ? "mip_timeout" : (dive.cut ? "dive_cut" : "no_improvement");
            }
        } else {
            result = dive.cut ? "dive_cut" : "no_improvement";
        }
        if (greedy_improved && result != "improved") result = "improved";   // the greedy repair already improved
        dive_steps_ += dive.steps;

        // adaptive weights (scores: new best 6, MIP proved no better 1, timeout 0.5, pruned 0.3)
        double score = result == "improved" ? 6.0 : (result == "no_improvement" || result == "dive_cut") ? 1.0
                     : result == "mip_timeout" ? 0.5 : 0.3;
        op.weight = std::max(0.05, 0.8 * op.weight + 0.2 * score);
        if (result == "improved") { op.improvements++; no_improve = 0; }
        else {
            no_improve++;
            if (result == "pruned") frac *= 1.15;                 // too small to contain an improvement
            else if (result == "mip_timeout") frac *= 0.85;       // too large for the repair MIP
            else if (no_improve % 4 == 0) frac *= 1.15;
            // stuck at the largest neighbourhoods: restart small (diversification)
            if (no_improve >= 12 && frac >= params_.lns_max_frac - 1e-9) {
                frac = params_.lns_init_frac;
                no_improve = 0;
            }
        }
        frac = std::clamp(frac, std::min(1.0, 2.0 / std::max(1, n)), std::max(params_.lns_init_frac, params_.lns_max_frac));

        std::cout << std::left << std::setw(6) << it << std::setw(10) << std::fixed << std::setprecision(1) << elapsed()
                  << std::setw(10) << op.name << std::setw(5) << k << std::setw(14) << std::setprecision(1) << cg.lb
                  << std::setw(12) << std::setprecision(0) << ub_ << result << "\n";
        if (trace_.enabled()) {
            JsonObj w;
            for (auto& o : ops_) w.num(o.name.c_str(), o.weight);
            trace_.write(JsonObj().str("ev", "lns").integer("it", it).num("t", elapsed()).num("dur", elapsed() - t_start)
                             .str("op", op.name).integer("k", k).num("frac", frac).num("lp", cg.lp).num("lb", cg.lb)
                             .str("result", result).num("value", new_value).num("ub", ub_)
                             .integer("cg_it", cg.iterations).integer("dive_steps", dive.steps)
                             .integer("pool", pool_.size()).raw("weights", w.dump()));
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

    if (trace_.enabled()) {
        std::string opstats = "[";
        for (size_t i = 0; i < ops_.size(); ++i) {
            if (i) opstats += ",";
            opstats += JsonObj().str("name", ops_[i].name).integer("tries", ops_[i].tries)
                           .integer("improvements", ops_[i].improvements).integer("pruned", ops_[i].pruned)
                           .num("weight", ops_[i].weight).dump();
        }
        opstats += "]";
        trace_.write(JsonObj().str("ev", "end").str("method", "lns").num("t", stats_.total_time_s).str("status", status)
                         .num("root_lb", root_lb_).num("lb", root_lb_).num("ub", ub_).num("gap", stats_.gap_pct)
                         .num("ttfs", ttfs_).integer("nodes", it).integer("pruned", pruned_count)
                         .integer("columns", pool_.size()).num("t_master", time_master_).num("t_pricing", time_pricing_)
                         .num("t_heur", time_mip_).num("t_greedy", time_greedy_).integer("greedy_improvements", greedy_improvements_)
                         .raw("operators", opstats));
        trace_.close();
    }
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "\n=======================================================\n";
    std::cout << " FIN LNS : " << instance_name << " (" << status << ")\n";
    std::cout << "   ├─ Temps total     : " << stats_.total_time_s << " s (master " << time_master_ << ", pricing "
              << time_pricing_ << ", MIP " << time_mip_ << ")\n";
    std::cout << "   ├─ Plongees        : " << dive_steps_ << " fixations au total\n";
    std::cout << "   ├─ Iterations      : " << it << " (" << pruned_count << " voisinages elagues par la borne, "
              << greedy_improvements_ << " ameliorations par l'insertion gloutonne en " << time_greedy_ << " s)\n";
    std::cout << "   ├─ Borne (racine)  : " << root_lb_ << "\n";
    std::cout << "   ├─ Meilleure sol.  : " << ub_ << "\n";
    std::cout << "   └─ Gap             : " << stats_.gap_pct << " %\n";
    for (auto& o : ops_)
        std::cout << "      " << std::setw(9) << o.name << " essais " << o.tries << ", ameliorations " << o.improvements
                  << ", elagues " << o.pruned << ", poids " << o.weight << "\n";
    std::cout << "=======================================================\n\n";
    return stats_;
}

void LnsSolver::export_best_solution(const std::string& filename) const {
    std::vector<Column> sol;
    for (const auto& cols : incumbent_)
        for (int c : cols) sol.push_back(pool_.get_column(c));
    write_solution_json(filename, graph_, sol, ub_, stats_, params_.link_conflicts);
}
