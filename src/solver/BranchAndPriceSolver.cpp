#include "BranchAndPriceSolver.h"
#include "SearchTree.h"
#include "PricingProblem.h"
#include "MasterProblem.h"
#include "SolutionWriter.h"
#include <map>
#include <cmath>
#include <algorithm>
#include <vector>
#include <numeric>
#include <stdexcept>
#include <stack>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <filesystem>

BranchingDecision BranchAndPriceSolver::choose_branching_variable() const {
    if (params_.branching == BranchingRule::QUANTUM) {
        return quantum_train_branching();
    }
    try {
        return service_split_branching();
    } catch (const std::runtime_error& e) {
        return quantum_train_branching();
    }
}

BranchingDecision BranchAndPriceSolver::service_split_branching() const {
    struct FractionalPath {
        const Column* col;
        double flow;
    };

    // 1. Récupération des chemins fractionnaires triés par train
    std::map<int, std::vector<FractionalPath>> train_fractional_paths;
    
    std::vector<int> active_ids = master_.get_active_columns_ids();
    for (int id : active_ids) {
        double val = master_.get_column_value(id);
        if (val > 1e-5 && val < 1.0 - 1e-5) {
            const Column& col = column_pool_.get_column(id);
            train_fractional_paths[col.train_id].push_back({&col, val});
        }
    }

    if (train_fractional_paths.empty()) {
        throw std::runtime_error("Trying to apply branching on an integer solution !");
    }

    BranchingDecision decision;
    decision.right_train_ids.resize(1);
    decision.left_train_ids.resize(1);
    
    // Variables pour garder la trace de la meilleure coupure trouvée
    int best_train_id = -1;
    int best_service_id = -1;
    int best_split_time = -1;
    double best_score = 1e9; // On cherche à minimiser ce score

    // 2. Recherche du meilleur candidat pour le branchement temporel
    for (const auto &[train_id, paths] : train_fractional_paths) {
        
        // Structure: Service_ID -> (Start_Time -> Cumulative Flow)
        // std::map trie naturellement les clés (les temps), ce qui est parfait pour nous !
        std::map<int, std::map<int, double>> service_times_flow;

        for (const auto& path : paths) {
            for (int arc_id : path.col->arc_ids) {
                const auto& arc = graph_.get_arc(arc_id);
                // Si c'est un arc de service, on enregistre la quantité de flux à cet instant
                // (Assure-toi que type enum SERVICE correspond bien à ton implémentation)
                if (arc.get_type() == SERVICE) { 
                    int s_id = arc.get_service();
                    // Assure-toi d'avoir un getter type get_start_time() sur ton Arc
                    service_times_flow[s_id][arc.get_start_time()] += path.flow; 
                }
            }
        }

        // On évalue la qualité de chaque coupure possible pour chaque service
        for (const auto& [s_id, time_flow] : service_times_flow) {
            // S'il n'y a qu'un seul instant de départ, on ne peut pas couper !
            if (time_flow.size() > 1) {
                
                double total_flow = 0.0;
                for (const auto& [time, flow] : time_flow) total_flow += flow;
                
                double cumulative_flow = 0.0;
                
                // On s'arrête à l'avant-dernier élément car on évalue la coupure "après" cet élément
                for (auto it = time_flow.begin(); std::next(it) != time_flow.end(); ++it) {
                    cumulative_flow += it->second;
                    
                    // 1er Objectif : Être proche de 50/50 (déséquilibre le plus proche de 0)
                    double balance_penalty = std::abs(cumulative_flow - (total_flow / 2.0));
                    
                    // 2ème Objectif : Maximiser le gap temporel entre cette option et la suivante
                    int next_time = std::next(it)->first;
                    int time_gap = next_time - it->first;
                    
                    // Le score final (on divise par le gap pour que les grands gaps réduisent le score)
                    double score = balance_penalty / static_cast<double>(time_gap);
                    
                    if (score < best_score) {
                        best_score = score;
                        best_train_id = train_id;
                        best_service_id = s_id;
                        best_split_time = it->first;
                    }
                }
            }
        }
    }

    // Sécurité : Si aucun service n'est fractionné temporellement
    if (best_train_id == -1) {
        throw std::runtime_error("Aucun service fractionne temporellement trouve. Il faut utiliser une autre regle (ex: quantum_train_branching) en secours !");
    }

// 3. Application de la décision
    decision.left_train_ids = {best_train_id};
    decision.right_train_ids = {best_train_id};
    
    std::vector<int> partition_A; // Arcs interdits pour la branche Gauche
    std::vector<int> partition_B; // Arcs interdits pour la branche Droite

    // OPTIMISATION : On cible directement et uniquement les arcs du service concerné
    const std::vector<int>& target_arcs = graph_.get_arcs_providing_service(best_service_id);
    
    for (int arc_id : target_arcs) {
        const Arc& arc = graph_.get_arc(arc_id);
        
        // BRANCHE GAUCHE : Le train DOIT faire le service <= best_split_time
        // -> On lui interdit tous les arcs de ce service qui partent STRICTEMENT APRES.
        if (arc.get_start_time() > best_split_time) {
            partition_A.push_back(arc_id);
        }
        
        // BRANCHE DROITE : Le train DOIT faire le service > best_split_time
        // -> On lui interdit tous les arcs de ce service qui partent AVANT OU PENDANT.
        if (arc.get_start_time() <= best_split_time) {
            partition_B.push_back(arc_id);
        }
    }

    decision.left_forbidden_arcs[best_train_id] = partition_A;
    decision.right_forbidden_arcs[best_train_id] = partition_B;

    decision.rule = "service_split";
    decision.train = best_train_id;
    decision.service = best_service_id;
    decision.split_time = best_split_time;
    decision.left_label = "T" + std::to_string(best_train_id) + " S" + std::to_string(best_service_id) + " start <= " + std::to_string(best_split_time);
    decision.right_label = "T" + std::to_string(best_train_id) + " S" + std::to_string(best_service_id) + " start > " + std::to_string(best_split_time);
    return decision;
}


BranchingDecision BranchAndPriceSolver::quantum_train_branching() const {
        
    // Structure locale pour stocker temporairement nos chemins fractionnaires
    struct FractionalPath {
        const Column* col;
        double flow;
    };
    
    // 1. Récupération des chemins fractionnaires triés par train
    std::map<int, std::vector<FractionalPath>> train_fractional_paths;
    
    std::vector<int> active_ids = master_.get_active_columns_ids();
    for (int id : active_ids) {
        double val = master_.get_column_value(id);
        // On cible strictement les variables fractionnaires (exclut les 0 et les 1)
        if (val > 1e-5 && val < 1.0 - 1e-5) {
            const Column& col = column_pool_.get_column(id);
            train_fractional_paths[col.train_id].push_back({&col, val});
        }
    }

    // Si tout est entier (ou vide), on s'arrête là !
    if (train_fractional_paths.empty()) {
        throw std::runtime_error("Trying to apply branching on an integer solution !");
    }

    BranchingDecision best_decision;
    best_decision.right_train_ids.resize(1);
    best_decision.left_train_ids.resize(1);
    std::vector<int> best_partition_A;
    std::vector<int> best_partition_B;
    int best_train_id = -1;
    int best_divergence_time = -1;


    // 2. Recherche du noeud de divergence pour chaque train
    double best_train_score = 1e9;
    for (const auto& [train_id, paths] : train_fractional_paths) {
        if (paths.empty()) continue;

        double total_fractional_flow = 0.0;
        // Parcours chronologique des arcs pour trouver la première divergence
        int divergence_idx = 0;
        bool diverged = false;
        
        for (int i = 0; i < paths[0].col->arc_ids.size(); ++i) {
            int first_arc = paths[0].col->arc_ids[i];
            // On vérifie si tous les chemins passent par cet arc à l'étape i
            for (const auto& p : paths) {
                if (p.col->arc_ids[i] != first_arc) {
                    diverged = true;
                    break;
                }
            }
            if (diverged) {
                divergence_idx = i;
                break;
            }
        }

        // 3. Collecte des arcs sortants au point de divergence et de leurs flux
        std::map<int, double> outgoing_arc_flows;
        if (diverged) {
            for (const auto& p : paths) {
                outgoing_arc_flows[p.col->arc_ids[divergence_idx]] += p.flow;
            }
        } else {
            throw std::runtime_error("Solution fractionnaire sans divergence, il y a probablement plusieurs fois la même colonne qui a été ajoutée !");
        }

        
        

        // 4. Partitionnement 50/50 avec Heuristique de Packing (Gloutonne)
        
        // On stocke les paires {flux, arc_id} pour pouvoir les trier
        std::vector<std::pair<double, int>> sorted_arcs;
        for (const auto& [arc, f] : outgoing_arc_flows) {
            sorted_arcs.push_back({f, arc});
        }

        // Étape A : On trie les arcs par flux décroissant (les plus gros blocs en premier)
        std::sort(sorted_arcs.begin(), sorted_arcs.end(), 
                  [](const std::pair<double, int>& a, const std::pair<double, int>& b) {
                      return a.first > b.first; 
                  });

        double current_sum_A = 0.0;
        double current_sum_B = 0.0;
        std::vector<int> partition_A;
        std::vector<int> partition_B;

        // Étape B : On distribue chaque arc dans le tas le plus "léger" à l'instant T
        for (const auto& item : sorted_arcs) {
            double flow = item.first;
            int arc_id = item.second;

            if (current_sum_A <= current_sum_B) {
                current_sum_A += flow;
                partition_A.push_back(arc_id);
            } else {
                current_sum_B += flow;
                partition_B.push_back(arc_id);
            }
        }

        // On évalue à quel point l'équilibre est parfait (idéalement l'écart est 0)
        double score = std::abs(current_sum_A - current_sum_B);

        // On sauvegarde si c'est la meilleure partition trouvée 
        if (score < best_train_score) {
            best_train_score = score;
            best_train_id = train_id;
            best_partition_A = partition_A;
            best_partition_B = partition_B;
            best_divergence_time = graph_.get_arc(paths[0].col->arc_ids[divergence_idx]).get_start_time();
        }

    }
    best_decision.left_train_ids = {best_train_id};
    best_decision.right_train_ids = {best_train_id};
    best_decision.left_forbidden_arcs[best_train_id] = best_partition_A;
    best_decision.right_forbidden_arcs[best_train_id] = best_partition_B;

    best_decision.rule = "quantum";
    best_decision.train = best_train_id;
    best_decision.divergence_time = best_divergence_time;
    best_decision.left_label = "T" + std::to_string(best_train_id) + " forbid " + std::to_string(best_partition_A.size()) + " arc(s) @t=" + std::to_string(best_divergence_time);
    best_decision.right_label = "T" + std::to_string(best_train_id) + " forbid " + std::to_string(best_partition_B.size()) + " arc(s) @t=" + std::to_string(best_divergence_time);
    return best_decision;
}

int BranchAndPriceSolver::add_Column(Column& col){
    int new_id = column_pool_.add_column(col);
    master_.add_column(col, graph_);
    state_manager_.register_column(col);
    return new_id;
}

//Initialisation des différentes parties ensemble. Pour être sur que tout est à jour à l'initialisation on fait bien attention à intialiser
//les colonnes correspondant au dummy_var dans le global state manager et la column pool (elles se font à la construction du rmp)
BranchAndPriceSolver::BranchAndPriceSolver(const TimeSpaceGraph& graph, const std::vector<Train>& trains, const SolverParams& params)
    :
    graph_(graph),
    trains_(trains),
    params_(params),
    master_(graph, trains, params.threads,
            (params.out_dir.empty() ? std::string("..") : params.out_dir) + "/gurobi_log.txt"),
    pricing_(graph, trains),
    state_manager_(trains.size(), graph.get_arcs().size()),
    tree_(),
    column_pool_()
{
    start_time_ = std::chrono::steady_clock::now();
    for(size_t k = 0; k < trains_.size(); k++){
        Column dummy_col = Column(k, {graph_.get_dummy_arc_id(k)}, std::vector<bool>(graph_.get_nb_services(), true), graph_.get_arc(graph_.get_dummy_arc_id(k)).get_cost(k), k);
        add_Column(dummy_col);
    }
}

std::vector<BPNode*> BranchAndPriceSolver::branch_on_node(BPNode* parent, const BranchingDecision& decision){
    BPNode* left = tree_.create_child_node(parent, decision.left_train_ids, decision.left_forbidden_arcs, decision.left_label);
    BPNode* right = tree_.create_child_node(parent, decision.right_train_ids, decision.right_forbidden_arcs, decision.right_label);
    return {left, right};
}

void BranchAndPriceSolver::switch_state(BPNode* target){
    Trajectory trajectory = tree_.trajectory_to_node(target);
    for(BPNode* to_rev: trajectory.nodes_to_revert){
        if(to_rev->train_ids.size() == 0){
            throw std::runtime_error("Trying to revert the root node. There is likely a problem with the computation of the trajectory to switch state in SearchTree");
        }
        ColumnList enable = state_manager_.revert_delta(to_rev->train_ids, to_rev->forbidden_arcs_ids);
        master_.enable_columns(enable);
    }
    for(BPNode* to_app: trajectory.nodes_to_apply){
        ColumnList disable = state_manager_.apply_delta(to_app->train_ids, to_app->forbidden_arcs_ids);
        master_.disable_columns(disable);
    }

}


bool BranchAndPriceSolver::is_current_solution_integer() const{
    switch(tree_.get_current_node()->status)
    {
        case(NodeStatus::FRACTIONAL):
            return false;
        case(NodeStatus::INTEGER):
            return true;
        case(NodeStatus::INFEASIBLE):
            return false;
        case(NodeStatus::PROCESSING):
            throw std::runtime_error("Trying to determine the state of the solution of a processing node");
        case(NodeStatus::PRUNED):
            throw std::runtime_error("Trying to determine the state of a pruned node");
        case(NodeStatus::UNPROCESSED):
            throw std::runtime_error("Trying to determine the state of an unprocessed node");
    }
    return false;
}


//The column generation algorithm suppose the state has been altered to reflect the node trying to be processed
void BranchAndPriceSolver::run_column_generation(BPNode* node, double termination_gap, const char* context) {
    if(node->status != NodeStatus::UNPROCESSED){
        throw std::runtime_error("Trying to run column generation on an already processed or processing node !");
    }
    node->status = NodeStatus::PROCESSING;
    // A node's bound can only improve on its parent's (inherited at creation):
    // the Lagrangian bound of an early-terminated CG may be weaker.
    const double inherited_lb = node->lower_bound;
    int cg_iter = 0;

    double best_lagrangian = -INF;
    while(true){
        if (stop_requested() || time_up()) {
            // node stays PROCESSING (the main loop stops right after); the best
            // Lagrangian bound seen so far is still a valid lower bound for it
            node->lower_bound = std::max(inherited_lb, best_lagrangian);
            return;
        }

        // ---------------- MASTER ----------------
        auto start_master = std::chrono::steady_clock::now();
        master_.solve();
        if (master_.is_interrupted()) {   // Ctrl-C caught by Gurobi
            request_stop();
            time_master_ += std::chrono::duration<double>(std::chrono::steady_clock::now() - start_master).count();
            return;
        }
        if(!master_.is_infeasible() && !master_.is_optimal()){
            throw std::runtime_error("Error while trying to solve the master problem at a given node (Gurobi status " + std::to_string(master_.get_gurobi_status()) + ")");
        }

        if(master_.is_infeasible()){
            node->status = NodeStatus::INFEASIBLE;
            node->lower_bound = INF;
            time_master_ += std::chrono::duration<double>(std::chrono::steady_clock::now() - start_master).count();
            break;
        }

        const std::vector<double> flow_duals = master_.get_flow_duals();
        const std::vector<std::vector<double>> service_duals = master_.get_service_duals();
        const std::vector<std::vector<double>> conflict_duals = master_.get_conflict_duals();
        time_master_ += std::chrono::duration<double>(std::chrono::steady_clock::now() - start_master).count();

        // ---------------- PRICING ----------------
        auto start_pricing = std::chrono::steady_clock::now();
        auto [add_cols, best_reduced_costs] = pricing_.solve(flow_duals, service_duals, conflict_duals, state_manager_);
        time_pricing_ += std::chrono::duration<double>(std::chrono::steady_clock::now() - start_pricing).count();

        const double rmp_value = master_.get_objective_value();
        double lagrangian_lower_bound = rmp_value + std::accumulate(best_reduced_costs.begin(), best_reduced_costs.end(), 0.0);
        double lagrangian_gap = std::abs((rmp_value - lagrangian_lower_bound)/ lagrangian_lower_bound);
        best_lagrangian = std::max(best_lagrangian, lagrangian_lower_bound);
        double node_bound = std::max(inherited_lb, best_lagrangian);

        if (trace_.enabled()) {
            double min_rc = 0.0;
            for (double rc : best_reduced_costs) min_rc = std::min(min_rc, rc);
            trace_.write(JsonObj().str("ev", "cg").str("ctx", context).integer("node", node->id)
                .integer("it", cg_iter).num("t", elapsed()).num("rmp", rmp_value)
                .num("lagr", lagrangian_lower_bound).integer("added", add_cols.size())
                .integer("pool", column_pool_.size()).num("min_rc", min_rc));
        }
        cg_iter++;

        if(node_bound > tree_.get_best_upper_bound() + 1e-5){
            node->status = NodeStatus::PRUNED;
            node->lower_bound = node_bound;
            return;
        }

        if(add_cols.empty() || lagrangian_gap <= termination_gap){
            node->lower_bound = node_bound;
            if (master_.is_sol_integer()){
                node->status = NodeStatus::INTEGER;
                if(tree_.get_best_upper_bound() > rmp_value){
                    update_incumbent(rmp_value, master_.get_active_columns_ids(), "node");
                }
            }
            else {
                node->status = NodeStatus::FRACTIONAL;
                if(node->lower_bound > tree_.get_best_upper_bound() + 1e-5){
                    node->status = NodeStatus::PRUNED;
                }
            }
            break;
        }

#ifdef TUSP_CHECK_REDUCED_COSTS
        // Debug check: the reduced cost found by the pricing must equal the one
        // implied by the master's duals and the column's coefficients
        // (cost - pi_k - sum alpha_ks - sum_{(r,t)} a_rt * mu_rt).
        for (const Column& col : add_cols) {
            double rc = col.cost - flow_duals[col.train_id];
            for (size_t s_id = 0; s_id < col.services.size(); ++s_id) {
                if (col.services[s_id] && trains_[col.train_id].get_service(s_id)) rc -= service_duals[col.train_id][s_id];
            }
            for (int a : col.arc_ids) {
                for (const auto& [r, t] : graph_.get_arc(a).get_iNodes()) rc -= conflict_duals[r][t / graph_.get_time_step()];
            }
            if (std::abs(rc - best_reduced_costs[col.train_id]) > 1e-6 * std::max(1.0, std::abs(rc))) {
                throw std::runtime_error("Reduced cost mismatch for train " + std::to_string(col.train_id) + ": pricing "
                    + std::to_string(best_reduced_costs[col.train_id]) + " vs master " + std::to_string(rc));
            }
            checked_columns_++;
        }
#endif
        for(auto& col : add_cols){
            add_Column(col);
        }
    }
}


BPNode* BranchAndPriceSolver::next_node() {
    BPNode* node = tree_.get_next_node();
    if (node != nullptr) {
        switch_state(node);
    }
    return node;
}


double BranchAndPriceSolver::elapsed() const {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start_time_).count();
}


std::string BranchAndPriceSolver::paths_json(const std::vector<int>& column_ids, bool with_flow) const {
    std::string s = "[";
    bool first = true;
    for (int id : column_ids) {
        const Column& col = column_pool_.get_column(id);
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


void BranchAndPriceSolver::trace_node(const BPNode* node, const char* event) const {
    if (!trace_.enabled() || node == nullptr) return;
    const char* status = "UNPROCESSED";
    switch (node->status) {
        case NodeStatus::UNPROCESSED: status = "UNPROCESSED"; break;
        case NodeStatus::PROCESSING:  status = "PROCESSING"; break;
        case NodeStatus::FRACTIONAL:  status = "FRACTIONAL"; break;
        case NodeStatus::INTEGER:     status = "INTEGER"; break;
        case NodeStatus::INFEASIBLE:  status = "INFEASIBLE"; break;
        case NodeStatus::PRUNED:      status = "PRUNED"; break;
    }
    trace_.write(JsonObj().str("ev", event).integer("id", node->id)
        .integer("parent", node->parent ? node->parent->id : -1).integer("depth", node->depth)
        .str("status", status).num("lb", node->lower_bound).str("label", node->label).num("t", elapsed()));
}


void BranchAndPriceSolver::update_incumbent(double value, const std::vector<int>& column_ids, const char* source) {
    std::vector<Column> incumbent = column_pool_.get_columns(column_ids);
    // Integer solution: every selected column is at 1, so the exact objective is the
    // sum of the (integer) column costs; avoids LP round-off such as 12258.9999962.
    double exact = 0.0;
    for (const Column& c : incumbent) exact += c.cost;
    if (std::abs(exact - value) < 1e-3 * std::max(1.0, std::abs(value))) value = exact;
    if (value >= tree_.get_best_upper_bound()) return;
    tree_.set_incumbent_solution(value, incumbent);
    std::cout << " * \033[32mNOUVELLE SOLUTION ENTIERE (" << source << ") : " << value << "\033[0m\n";
    if (trace_.enabled()) {
        trace_.write(JsonObj().str("ev", "incumbent").num("t", elapsed()).num("value", value)
            .str("source", source).raw("train_paths", paths_json(column_ids, false)));
    }
}


SolveStatistics BranchAndPriceSolver::solve(double time_limit_s, const std::string& instance_name) {

    print_log_header();
    start_time_ = std::chrono::steady_clock::now();
    time_limit_s_ = time_limit_s;
    const std::vector<double>& gap_limits = params_.cg_gaps;

    SolveStatistics stats;
    stats.instance_name = instance_name;
    stats.status = "OPTIMAL";
    stats.num_trains = trains_.size();
    stats.num_arcs = graph_.get_arcs().size();
    stats.time_to_first_sol_s = -1.0;
    stats.root_lb = -INF;
    stats.pruned_nodes = 0;

    const std::string out_dir = params_.out_dir.empty() ? std::string("../results/runs/") + instance_name : params_.out_dir;
    std::filesystem::create_directories(out_dir);

    // Legacy dynamics file (kept for compatibility with the old plots)
    std::ofstream log_file(out_dir + "/dynamics.csv");
    log_file << "Time_s,Nodes,Columns,LB,UB,Gap\n";

    if (params_.trace) {
        trace_.open(out_dir + "/trace.jsonl");
        std::string gaps = "[";
        for (size_t i = 0; i < gap_limits.size(); ++i) gaps += (i ? "," : "") + std::to_string(gap_limits[i]);
        gaps += "]";
        trace_.write(JsonObj().str("ev", "start").str("instance", instance_name).str("method", "bp")
            .integer("trains", trains_.size()).integer("arcs", graph_.get_arcs().size())
            .integer("T", graph_.get_horizon()).integer("t_s", graph_.get_time_step())
            .raw("params", JsonObj().num("time_limit_s", time_limit_s).str("method", "bp").raw("cg_gaps", gaps)
                .integer("heuristic_every", params_.heuristic_every).boolean("diving", params_.diving)
                .num("dive_cg_gap", params_.dive_cg_gap).boolean("mip_heuristic", params_.mip_heuristic)
                .num("mip_time_s", params_.mip_time_s)
                .str("branching", params_.branching == BranchingRule::QUANTUM ? "quantum" : "auto")
                .str("search", params_.start_dfs ? "dfs" : "bbf").integer("threads", params_.threads)
                .boolean("link_conflicts", params_.link_conflicts)
                .integer("conflict_rows", static_cast<long long>(graph_.get_nb_conflict_resources()) * (graph_.get_horizon() / graph_.get_time_step() + 1)).dump()));
    }
    if (params_.start_dfs) {
        tree_.set_strategy_DFS();
    }
    SearchStrategy last_strategy = tree_.get_current_strategy();

    int iter = 0;
    BPNode* current_node = tree_.get_next_node();
    switch_state(current_node);

    while (current_node != nullptr) {

        if (iter > 0 && iter % 20 == 0) {
            print_log_header();
        }

        double elapsed_s = elapsed();
        if (stop_requested()) {
            std::cout << "\nARRET DEMANDE. Arret du solveur.\n";
            stats.status = "INTERRUPTED";
            break;
        }
        if (elapsed_s >= time_limit_s) {
            std::cout << "\nTIME LIMIT ATTEINTE (" << time_limit_s << "s). Arret du solveur.\n";
            stats.status = "TIME_LIMIT";
            break;
        }

        double current_lb = tree_.get_best_lower_bound();
        double current_ub = tree_.get_best_upper_bound();
        double current_gap = (current_ub < INF && current_lb > 0) ? ((current_ub - current_lb) / current_lb * 100.0) : NAN;

        log_file << elapsed_s << "," << iter << "," << column_pool_.size() << ","
                 << (current_lb > -INF ? std::to_string(current_lb) : "") << "," << (current_ub < INF ? std::to_string(current_ub) : "") << ","
                 << (std::isnan(current_gap) ? 0.0 : std::max(0.0, current_gap)) << "\n";
        log_file.flush();

        if (trace_.enabled()) {
            if (tree_.get_current_strategy() != last_strategy) {
                last_strategy = tree_.get_current_strategy();
                trace_.write(JsonObj().str("ev", "strategy").num("t", elapsed_s)
                    .str("value", last_strategy == SearchStrategy::DFS ? "DFS" : "BBF"));
            }
            trace_.write(JsonObj().str("ev", "bounds").num("t", elapsed_s).integer("it", iter)
                .integer("node", current_node->id).integer("depth", current_node->depth)
                .num("lb", current_lb).num("ub", current_ub).num("gap", current_gap)
                .integer("pool", column_pool_.size()).integer("open", tree_.count_open_nodes())
                .num("t_master", time_master_).num("t_pricing", time_pricing_).num("t_heur", time_heuristics_));
        }

        // Node already dominated by the incumbent: prune without solving it.
        if(current_ub <= current_node->lower_bound + 1e-6){
            BPNode* pruned = current_node;
            current_node->status = NodeStatus::PRUNED;
            trace_node(pruned, "node");
            stats.pruned_nodes++;
            current_node = next_node();   // switch state BEFORE prune() clears the node's restrictions
            tree_.prune(pruned);
            iter++;
            continue;
        }

        std::string current_action = "CG";

        // --- Primal heuristics ---
        if (params_.heuristic_every > 0 && iter > 0 && iter % params_.heuristic_every == 0
            && (params_.diving || params_.mip_heuristic)) {
            current_action = "Heur.";
            print_log_line(iter, current_node->depth, elapsed_s, current_lb, current_ub, current_action);
            auto h_start = std::chrono::steady_clock::now();
            double ub_before = tree_.get_best_upper_bound();
            if (params_.diving && !stop_requested() && !time_up()) {
                run_diving_heuristic(*current_node);
            }
            if (params_.mip_heuristic && !stop_requested() && !time_up()) {
                run_global_mip_heuristic(std::min(params_.mip_time_s, std::max(1.0, time_limit_s - elapsed())));
            }
            double h_time = std::chrono::duration<double>(std::chrono::steady_clock::now() - h_start).count();
            time_heuristics_ += h_time;
            if (trace_.enabled()) {
                trace_.write(JsonObj().str("ev", "heuristic").num("t", elapsed()).num("duration", h_time)
                    .num("ub_before", ub_before).num("ub_after", tree_.get_best_upper_bound()));
            }
            if (stop_requested() || time_up()) continue;   // handled at the top of the loop
        }
        current_ub = tree_.get_best_upper_bound();
        if (current_ub < INF && stats.time_to_first_sol_s < 0) {
            stats.time_to_first_sol_s = elapsed();
        }

        // --- Column generation on the node ---
        double gap_limit = gap_limits.back();
        if (current_node->depth == 0) gap_limit = gap_limits[0];
        else if (current_node->depth <= 5) gap_limit = gap_limits[1];
        else if (current_node->depth <= 10) gap_limit = gap_limits[2];
        run_column_generation(current_node, gap_limit);
        if (stop_requested() || time_up()) {
            if (current_node->depth == 0) stats.root_lb = current_node->lower_bound;
            continue;
        }

        if (stats.time_to_first_sol_s < 0 && tree_.get_best_upper_bound() < INF) {
            stats.time_to_first_sol_s = elapsed();
        }

        // Root node: bound + fractional LP solution snapshot
        if (current_node->depth == 0 && current_node->status != NodeStatus::INFEASIBLE) {
            stats.root_lb = current_node->lower_bound;
            if (trace_.enabled() && params_.lp_snapshot) {
                trace_.write(JsonObj().str("ev", "lp").integer("node", current_node->id).num("t", elapsed())
                    .num("value", master_.get_objective_value())
                    .raw("train_paths", paths_json(master_.get_active_columns_ids(), true)));
            }
        }
        trace_node(current_node, "node");

        if(current_node->status == NodeStatus::PRUNED){
            current_action = "Pruning";
            stats.pruned_nodes++;
        }
        else if (current_node->status == NodeStatus::INFEASIBLE) {
            current_action = "Infeasible";
        }
        else if (current_node->status == NodeStatus::INTEGER) {
            current_action = "Integer";
        }
        else if (current_node->status == NodeStatus::FRACTIONAL) {
            current_action = "Branching";
            BranchingDecision decision = choose_branching_variable();
            std::vector<BPNode*> children = branch_on_node(current_node, decision);
            if (trace_.enabled()) {
                std::string kids = "[";
                for (size_t i = 0; i < children.size(); ++i) {
                    if (i) kids += ',';
                    kids += JsonObj().integer("id", children[i]->id).str("label", children[i]->label).dump();
                }
                kids += "]";
                trace_.write(JsonObj().str("ev", "branch").num("t", elapsed()).integer("parent", current_node->id)
                    .str("rule", decision.rule).integer("train", decision.train).integer("service", decision.service)
                    .integer("split_time", decision.split_time).integer("divergence_time", decision.divergence_time)
                    .raw("children", kids));
            }
        }

        print_log_line(iter, current_node->depth, elapsed_s, current_lb, current_ub, current_action);

        BPNode* processed = current_node;
        current_node = next_node();
        if (processed->status == NodeStatus::PRUNED) {
            tree_.prune(processed);
        }
        iter++;
    }

    log_file.close();

    // --- STATISTIQUES FINALES ---
    stats.total_time_s = elapsed();
    stats.final_lb = tree_.get_best_lower_bound();
    stats.final_ub = tree_.get_best_upper_bound();
    // every cost is >= 0: a negative Lagrangian bound (unfinished CG) is replaced by 0,
    // and the gap is undefined (NaN -> null in the trace) without a positive bound
    stats.root_lb = std::max(0.0, stats.root_lb);
    stats.final_lb = std::max(0.0, stats.final_lb);
    stats.gap_pct = (stats.final_ub < 1e9 && stats.final_lb > 0)
               ? std::max(0.0, (stats.final_ub - stats.final_lb) / stats.final_lb * 100.0) : NAN;
    stats.explored_nodes = iter;
    stats.num_columns = column_pool_.size();
    stats.time_in_master_s = time_master_;
    stats.time_in_pricing_s = time_pricing_;
    stats.time_in_heuristics_s = time_heuristics_;
    if (stats.final_ub >= 1e9 && stats.status == "OPTIMAL") {
        stats.status = "INFEASIBLE";
    }
    last_stats_ = stats;

    if (trace_.enabled()) {
        trace_.write(JsonObj().str("ev", "end").num("t", stats.total_time_s).str("status", stats.status)
            .num("root_lb", stats.root_lb).num("lb", stats.final_lb).num("ub", stats.final_ub)
            .num("gap", stats.gap_pct).num("ttfs", stats.time_to_first_sol_s)
            .integer("nodes", stats.explored_nodes).integer("pruned", stats.pruned_nodes)
            .integer("columns", stats.num_columns).num("t_master", time_master_)
            .num("t_pricing", time_pricing_).num("t_heur", time_heuristics_));
        trace_.close();
    }

    std::cout << "\n=======================================================\n";
    std::cout << " FIN DE LA RÉSOLUTION : " << stats.instance_name << " (" << stats.status << ")\n";
    std::cout << "=======================================================\n";

    std::cout << std::fixed << std::setprecision(2);

    std::cout << "   PROFILING DU TEMPS :\n";
    std::cout << "   ├─ Temps Total    : " << stats.total_time_s << " s\n";
    if (stats.time_to_first_sol_s > 0) {
        std::cout << "   ├─ 1ère solution  : " << stats.time_to_first_sol_s << " s\n";
    }
    double safe_total = std::max(stats.total_time_s, 0.001);
    std::cout << "   ├─ Master : " << stats.time_in_master_s << " s ("
              << (stats.time_in_master_s / safe_total * 100.0) << "%)\n";
    std::cout << "   ├─ Pricing        : " << stats.time_in_pricing_s << " s ("
              << (stats.time_in_pricing_s / safe_total * 100.0) << "%)\n";
    std::cout << "   └─ Heuristiques primales : " << stats.time_in_heuristics_s << " s (inclut une partie du temps de Master/Pricing)\n";

    std::cout << "\n  RÉSULTATS:\n";
    std::cout << "   ├─ Noeuds explorés: " << stats.explored_nodes << "\n";
    std::cout << "   ├─ LB Racine      : " << stats.root_lb << "\n";
    std::cout << "   ├─ LB Finale      : " << stats.final_lb << "\n";
    if (stats.final_ub < 1e9) {
        std::cout << "   ├─ UB Finale      : " << stats.final_ub << "\n";
        if (std::isnan(stats.gap_pct)) std::cout << "   └─ Gap Prouvé     : indefini (LB <= 0 : racine non convergee)\n";
        else std::cout << "   └─ Gap Prouvé     : " << stats.gap_pct << " %\n";
    } else {
        std::cout << "   ├─ UB Finale      : INF (Aucune solution entière trouvée)\n";
        std::cout << "   └─ Gap Prouvé     : INF\n";
    }
    std::cout << "=======================================================\n\n";
#ifdef TUSP_CHECK_REDUCED_COSTS
    std::cout << "[check] reduced costs verified on " << checked_columns_ << " columns\n";
#endif

    return stats;
}

double BranchAndPriceSolver::get_optimal_value() const {
    return tree_.get_best_upper_bound();
}

std::vector<Column> BranchAndPriceSolver::get_optimal_solution() const{
    return tree_.get_incumbent_solution();
}


void BranchAndPriceSolver::run_global_mip_heuristic(double time_limit_seconds) {

    // On récupère toutes les colonnes qui sont actuellement interdites par le noeud courant
    std::vector<int> cols_to_restore{};
    for(int column_id = 0; column_id < column_pool_.size(); column_id++){
        if(state_manager_.is_column_disabled(column_id)){
            cols_to_restore.push_back(column_id);
        }
    }

    master_.enable_columns(cols_to_restore);

    master_.convert_to_integer();
    master_.set_time_limit(time_limit_seconds);
    master_.set_focus_to_finding_sol();

    master_.solve();

    if (!master_.is_infeasible() &&  master_.get_current_nb_solutions() > 0) {
        double heuristic_val = master_.get_objective_value();
        if (heuristic_val < tree_.get_best_upper_bound()) {
            update_incumbent(heuristic_val, master_.get_active_columns_ids(), "mip");
        }
    }

    master_.set_time_limit(GRB_INFINITY);
    master_.set_focus_to_auto();
    master_.convert_to_continuous();
    master_.disable_columns(cols_to_restore);

}


struct DiveStep {
    std::vector<int> train_ids;
    std::map<int, std::vector<int>> forbidden_arcs;
};

void BranchAndPriceSolver::run_diving_heuristic(BPNode base_node) {

    std::stack<DiveStep> dive_history;
    int depth_dive = 0;

    while (true) {
        depth_dive++;

        // CG rapide (gap relâché)
        run_column_generation(&base_node, params_.dive_cg_gap, "dive");
        if (stop_requested() || time_up()) {
            break;
        }
        base_node.status = NodeStatus::UNPROCESSED;
        if (base_node.lower_bound > tree_.get_best_upper_bound()){
            break;
        }
        if (master_.is_infeasible()) {
            break;
        }

        if (master_.is_sol_integer()) {
            double val = master_.get_objective_value();
            if (val < tree_.get_best_upper_bound()) {
                update_incumbent(val, master_.get_active_columns_ids(), "diving");
            }
            break;
        }

        // Variable fractionnaire la plus proche de 1
        int best_col_id = -1;
        double best_val = -1;
        for (int id : master_.get_active_columns_ids()) {
            double val = master_.get_column_value(id);
            if (val > 1e-5 && val < 1.0 - 1e-5) {
                if (val > best_val) {
                    best_val = val;
                    best_col_id = id;
                }
            }
        }

        if (best_col_id == -1) {
            throw std::runtime_error (" [DIVING] Aucune variable fractionnaire exploitable alors que l'intégrité a été testé en amont.");
        }

        // Fixing : on interdit au train cible tous les arcs hors de la colonne choisie
        const Column& chosen_col = column_pool_.get_column(best_col_id);
        int target_train = chosen_col.train_id;

        std::vector<bool> in_column(graph_.get_arcs().size(), false);
        for (int a : chosen_col.arc_ids) in_column[a] = true;
        std::vector<int> arcs_to_forbid;
        for (const Arc& arc : graph_.get_arcs()) {
            if (!in_column[arc.get_id()]) {
                arcs_to_forbid.push_back(arc.get_id());
            }
        }

        std::vector<int> step_train_ids = {target_train};
        std::map<int, std::vector<int>> step_delta_map;
        step_delta_map[target_train] = arcs_to_forbid;

        ColumnList disabled_cols = state_manager_.apply_delta(step_train_ids, step_delta_map);
        master_.disable_columns(disabled_cols);
        dive_history.push({step_train_ids, step_delta_map});
    }

    // Dépilage des modifications
    while (!dive_history.empty()) {
        DiveStep last_step = dive_history.top();
        dive_history.pop();
        ColumnList enabled_cols = state_manager_.revert_delta(last_step.train_ids, last_step.forbidden_arcs);
        master_.enable_columns(enabled_cols);
    }
}

void BranchAndPriceSolver::export_best_solution(const std::string& filename) const {
    write_solution_json(filename, graph_, tree_.get_incumbent_solution(), tree_.get_best_upper_bound(),
                        last_stats_, params_.link_conflicts);
}

void BranchAndPriceSolver::print_log_header() const {
    std::cout << "\n" << std::string(90, '=') << "\n";
    std::cout << std::left 
              << std::setw(8)  << "Node" 
              << std::setw(8)  << "Depth" 
              << std::setw(10) << "Cols" 
              << std::setw(12) << "Time (s)" 
              << std::setw(15) << "Best Bound" 
              << std::setw(15) << "Incumbent" 
              << std::setw(10) << "Gap (%)" 
              << std::setw(12) << "Action" 
              << "\n";
    std::cout << std::string(90, '-') << "\n";
}

void BranchAndPriceSolver::print_log_line(int iter, int depth, double elapsed_s, double lb, double ub, const std::string& action) const {
    // Calcul du gap sécurisé
    double gap = (ub < 1e9 && lb > -1e9 && lb != 0) ? ((ub - lb) / lb * 100.0) : 0.0;
    if (gap < 0) gap = 0.0;

    // Formatage de l'UB (pour ne pas afficher 1e+09)
    std::string ub_str = (ub < 1e9) ? std::to_string(ub) : "---";

    std::cout << std::left 
              << std::setw(8)  << iter 
              << std::setw(8)  << depth 
              << std::setw(10) << column_pool_.size() 
              << std::fixed << std::setprecision(1) << std::setw(12) << elapsed_s 
              << std::setprecision(2) << std::setw(15) << (lb > -INF ? std::to_string(lb).substr(0, 8) : "---")
              << std::setw(15) << (ub < INF ? std::to_string(ub).substr(0, 8) : "---") 
              << std::setprecision(2) << std::setw(10) << (ub < 1e9 ? std::to_string(gap).substr(0, 5) + "%" : "---")
              << std::setw(12) << action 
              << "\n";
}