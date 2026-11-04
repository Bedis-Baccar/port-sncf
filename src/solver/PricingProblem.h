#pragma once

#include <vector>
#include <optional>
#include "TimeSpaceGraph.h"
#include "Train.h"
#include "Column.h"
#include "GlobalStateManager.h"


class PricingProblem {
private:
    const TimeSpaceGraph& graph_;
    const std::vector<Train>& trains_;

    // Constante pour l'algorithme de plus court chemin
    static constexpr double INF = 1e9; 

    //Préallocation des tableaux pour la programmation dynamique pour éviter d'avoir a réallouer à chaque pricing
    //On leur alloue un majorant de la taille soit total_nodes * 2^num_services
    mutable std::vector<double> dist_;
    mutable std::vector<int> prev_state_;
    mutable std::vector<int> prev_arc_;

    // Calcule le coût réduit d'un arc à la volée définit comme étant le cout de l'arc - le cout des conflits associées à l'arc (la logique de service est implémenté dans l'algo de recherche)
    double get_arc_reduced_cost(
        int train_id, 
        const Arc& arc, 
        const std::vector<std::vector<double>>& conflict_duals
    ) const;

public:
    PricingProblem(const TimeSpaceGraph& graph, const std::vector<Train>& trains);

    std::pair<std::vector<Column>, std::vector<double>> solve(
        const std::vector<double>& flow_duals, 
        const std::vector<std::vector<double>>& service_duals, 
        const std::vector<std::vector<double>>& conflict_duals,
        const GlobalStateManager& state_manager
    ) const;

    // Résout le pricing pour UN train spécifique 
    std::pair<Column, double> find_shortest_path_for_train(
        int train_id,
        double pi_k, 
        const std::vector<double>& service_duals_k,
        const std::vector<std::vector<double>>& conflict_duals,
        const GlobalStateManager& state_manager
    ) const;
};