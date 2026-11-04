#include "PricingProblem.h"
#include <algorithm>
#include <iostream>
#include "GlobalStateManager.h"

PricingProblem::PricingProblem(const TimeSpaceGraph& graph, const std::vector<Train>& trains):
graph_(graph), 
trains_(trains)
{
    //On préalloue les structures de la programmation dynamique avec des tailles convenables
    int max_services = 0;
    for (const Train& train : trains_) {
        
        int nb_services = train.get_services().size(); 
        if (nb_services > max_services) {
            max_services = nb_services;
        }
    }
    
    int max_mask_count = 1 << max_services;
    int max_total_states = graph_.get_nodes().size() * max_mask_count;
    
    // On pré-alloue avec la taille maximale
    dist_.resize(max_total_states);
    prev_state_.resize(max_total_states);
    prev_arc_.resize(max_total_states);
}

double PricingProblem::get_arc_reduced_cost(int train_id, const Arc& arc, const std::vector<std::vector<double>>& conflict_duals) const {
    double r_cost = arc.get_cost(train_id);
    int t_s = graph_.get_time_step();
    
    // On soustrait la pénalité de conflit pour chaque nœud physique occupé à chaque instant
    for (const auto& node_time : arc.get_iNodes()) {
        int p = node_time.first;
        int t = node_time.second;
        r_cost -= conflict_duals.at(p).at(t / t_s);
    }
    return r_cost;
}


std::pair<std::vector<Column>, std::vector<double>> PricingProblem::solve(const std::vector<double>& flow_duals, const std::vector<std::vector<double>>& service_duals, const std::vector<std::vector<double>>& conflict_duals, const GlobalStateManager& state_manager) const {
    std::vector<Column> new_columns;
    std::vector<double> best_reduced_costs(trains_.size(), 0.0);
    for (const Train& train : trains_) {
        int k = train.get_ID();
        // On lance le pricing pour le train k
        auto [opt_col,reduced_cost] = find_shortest_path_for_train(k, flow_duals[k], service_duals[k], conflict_duals, state_manager);
        
        // Si une colonne (chemin de coût réduit < 0) a été trouvée, on l'ajoute
        if (reduced_cost <= -1e-6) {
            new_columns.push_back(opt_col);
            best_reduced_costs[k] = reduced_cost;
        }
    }
    return {new_columns, best_reduced_costs};
}

std::pair<Column, double> PricingProblem::find_shortest_path_for_train(int train_id, double pi_k, const std::vector<double>& service_duals_k, const std::vector<std::vector<double>>& conflict_duals, const GlobalStateManager& state_manager) const{
    
    const Train& train = trains_[train_id];
    int ns = service_duals_k.size();
    
    // 1. Préparation des masques binaires pour les services requis
    std::vector<int> service_bit(ns, -1);
    int bit_idx = 0;
    for (int s = 0; s < ns; s++) {
        if (train.get_service(s)) {
            service_bit[s] = bit_idx++; // Associe le service à un bit spécifique
        }
    }
    
    int mask_count = 1 << bit_idx; // 2^(nombre de services requis)
    int total_nodes = graph_.get_nodes().size();
    int total_states = total_nodes * mask_count;
    
    // 2. Initialisation des structures de la programmation dynamique
    std::fill(dist_.begin(), dist_.begin() + total_states, INF);
    std::fill(prev_state_.begin(), prev_state_.begin() + total_states, -1);
    std::fill(prev_arc_.begin(), prev_arc_.begin() + total_states, -1);
    
    // Fonction lambda utile pour aplatir le tableau 2D (Noeud x Masque)
    auto state_index = [&](int node_id, int mask) { return node_id * mask_count + mask; };
    
    // Le départ se fait depuis le nœud source (ID 0) avec aucun service accompli (Masque 0)
    dist_[state_index(0, 0)] = 0.0;

    // 3. Fonction de relaxation locale
    auto relax_from_node = [&](int node_id) {
        const Node& node = graph_.get_node(node_id);
        
        for (int mask = 0; mask < mask_count; mask++) {
            double d0 = dist_[state_index(node_id, mask)];
            if (d0 >= INF / 2.0) continue; // État inatteignable, on ignore
            for (int a_id : node.getFromArcs()) {
                if(state_manager.is_arc_forbidden(train_id, a_id)){
                    continue;
                }
                const Arc& arc = graph_.get_arc(a_id);
                

                if (!graph_.arc_accessible_by_train(train_id, a_id)) continue; 

                // Coût de base + Pénalité de conflit
                double arc_r_cost = get_arc_reduced_cost(train_id, arc, conflict_duals);
                int new_mask = mask;
                
                // Traitement spécifique si c'est un arc de Service
                if (arc.get_type() == SERVICE) {
                    int s_id = arc.get_service();
                    int bit = service_bit[s_id];
                    
                    if (bit >= 0) {
                        // Si le service est requis ET n'a pas encore été accompli
                        if ((mask & (1 << bit)) == 0) {
                            arc_r_cost -= service_duals_k[s_id]; // On soustrait Alpha (la récompense duale)
                            new_mask = mask | (1 << bit);            // On met à jour le masque
                        } else {
                            // Si le service est déjà accompli, on s'interdit de le refaire (ou on ne donne pas la prime)
                            continue; 
                        }
                    }
                }
                
                double d = d0 + arc_r_cost;
                int n2 = arc.get_to();
                int next_state = state_index(n2, new_mask);
                
                // Mise à jour si le nouveau chemin est meilleur
                if (d < dist_[next_state]) {
                    dist_[next_state] = d;
                    prev_state_[next_state] = state_index(node_id, mask);
                    prev_arc_[next_state] = a_id;
                }
            }
        }
    };

    // 4. Exécution du tri topologique (Parcours du DAG dans le temps)
    relax_from_node(0); // On lance depuis la source
    
    int T = graph_.get_horizon();
    int t_s = graph_.get_time_step();
    int num_pnodes = graph_.get_nb_pnodes(); // <-- Assure-toi d'avoir cette méthode dans TimeSpaceGraph !
    
    for (int t = 0; t <= T; t += t_s) {
        for (int p = 0; p < num_pnodes; p++) {
            for (int l = 0; l < 3; l++) {
                int n_id = graph_.compute_virtual_node_id(p, t, l);
                relax_from_node(n_id);
            }
        }
    }
    

    int full_mask = mask_count - 1; // Tous les bits à 1 = tous les services requis sont faits
    int s = state_index(1, full_mask); 
    double best_rcost = dist_[s];
    int best_state = s;
    

    // Si on n'a rien trouvé d'atteignable
    if (best_state < 0 || best_rcost >= INF / 2.0) {
        return {Column(), INF};
    }

    double total_reduced_cost = best_rcost - pi_k;

    // 7. Reconstruction du chemin et création de la Colonne "Propre"
    Column new_col;
    new_col.train_id = train_id;
    new_col.cost = 0; // On va recalculer le vrai coût (sans les duales)
    new_col.services = std::vector<bool>(ns, false);
    
    int cur = best_state;
    std::vector<int> path_arcs;
    
    while (cur != state_index(0, 0)) {
        int a_id = prev_arc_[cur];
        if (a_id < 0) break;
        path_arcs.push_back(a_id);
        
        const Arc& arc = graph_.get_arc(a_id);
        new_col.cost += arc.get_cost(train_id);
        
        if (arc.get_type() == SERVICE) {
            new_col.services[arc.get_service()] = true;
        }
        
        cur = prev_state_[cur];
    }
    
    // On remet les arcs dans le bon ordre chronologique (de la source vers le puits)
    std::reverse(path_arcs.begin(), path_arcs.end());
    new_col.arc_ids = path_arcs;

    return {new_col,total_reduced_cost};
}