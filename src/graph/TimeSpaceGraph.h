#pragma once
#include <vector>
#include "Node.h"
#include "Arc.h"

class TimeSpaceGraph {
private:
    std::vector<Node> nodes_;
    std::vector<Arc> arcs_;
    std::vector<std::vector<int>> arc_providing_services_; //arc_providing_services[service_id] gives back the ids of all arcs providing a given service
    std::vector<std::vector<bool>> arc_accessible_by_train_; // for a given train_id gives back a boolean vector representing the arcs accessible by said train
    std::vector<int> dummy_arcs_; //For a given train k gives back the id of its dummy arc
    // Métadonnées du réseau
    int T_;               // Horizon de temps
    int t_s_;             // Pas de temps
    int num_pnodes_;      // Nombre de nœuds physiques
    int num_services_;
public:

    TimeSpaceGraph(int time_horizon, int time_step, int num_physical_nodes, int num_services, std::vector<Node> nodes, std::vector<Arc> arcs, std::vector<std::vector<int>> arc_providing_services, std::vector<std::vector<bool>> arc_accessible_by_train, std::vector<int> dummy_arcs);

    // Ajout d'éléments
    void add_node(const Node& node);
    void add_arc(const Arc& arc);

    // Accesseurs
    const std::vector<Node>& get_nodes() const;
    const std::vector<Arc>& get_arcs() const;
    const Node& get_node(int id) const;
    const Arc& get_arc(int id) const;
    int get_nb_pnodes() const {return num_pnodes_;}
    int get_nb_services() const {return num_services_;}
    int get_dummy_arc_id(int k) const{return dummy_arcs_[k];}
    const std::vector<int>& get_arcs_providing_service(int service_id) const;

    
    // Utilitaires
    int get_horizon() const { return T_; }
    int get_time_step() const { return t_s_; }
    bool arc_accessible_by_train(int train_id, int arc_id) const {return arc_accessible_by_train_[train_id][arc_id];}
    
    // Remplace la fonction globale n_id
    int compute_virtual_node_id(int p, int t, int l) const;
    static int compute_virtual_node_id(int p, int t, int l, int time_step, int num_pnodes);
};