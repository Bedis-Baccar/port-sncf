#pragma once

#include <vector>
#include <memory>
#include "gurobi_c++.h"
#include "Column.h"
#include "TimeSpaceGraph.h"
#include "Train.h"

enum class MasterStatus{
    SOLVED,
    UNSOLVED
};

class MasterProblem {
private:
    std::unique_ptr<GRBEnv> env_;
    std::unique_ptr<GRBModel> model_;

    int num_trains_;
    int num_services_;
    int num_pnodes_;
    int max_time_;
    int time_step_;
    MasterStatus status_;

    // --- Variables ---
    std::vector<GRBVar> column_vars_{};  //On suppose que les num_trains première variable sont les dummy variables pour chaque train 

    // --- Contraintes (Fini les tableaux plats !) ---
    // 1. Contraintes de flux (1 chemin par train) : Taille [num_trains]
    std::vector<GRBConstr> flow_constrs_; 
    
    // 2. Contraintes de service (1 par train et par service requis) : Taille [num_trains][num_services]
    std::vector<std::vector<GRBConstr>> service_constrs_;
    std::vector<std::vector<bool>> service_required_;  // Pour chaque train k renvoie le vecteur de booléens correspondant au service
    
    // 3. Contraintes de conflit (Nœuds physiques vs Temps) : Taille [num_pnodes][(max_time / time_step)]
    std::vector<std::vector<GRBConstr>> conflict_constrs_; 

    // --- Méthode interne ---
    void build_initial_model(const std::vector<Train>& trains, const TimeSpaceGraph& graph);

public:
    MasterProblem(const TimeSpaceGraph& graph, const std::vector<Train>& trains);
    ~MasterProblem();

    // --- Résolution continue (Column Generation) ---
    void solve();
    bool is_optimal() const;
    bool is_infeasible() const;
    double get_objective_value() const;
    int get_gurobi_status() const;

    // --- Extraction des variables duales ---

    std::vector<double> get_flow_duals() const;                            // Retourne Pi
    std::vector<std::vector<double>> get_service_duals() const;            // Retourne Alpha
    std::vector<std::vector<double>> get_conflict_duals() const;           // Retourne Mu
    double get_column_value(int id) const;
    MasterStatus get_master_status() const;
    int get_current_nb_solutions() const;

    bool is_sol_integer() const; 
    bool is_sol_fractional() const;
    // --- Ajout dynamique ---
    // Reçoit une colonne, crée la GRBVar correspondante, et met des "1" dans les bonnes contraintes
    void add_column(const Column& col, const TimeSpaceGraph& graph);
    void enable_columns(const std::vector<int>& col_id);
    void disable_columns(const std::vector<int>& col_id);

    void convert_to_integer();
    void convert_to_continuous();
    std::vector<int> get_active_columns_ids() const; //Renvoie l'id des colonnes qui sont actives (ie de valeur non nulles)
    void set_time_limit(double time_limit_second);
    void set_focus_to_finding_sol();
    void set_focus_to_auto();
};