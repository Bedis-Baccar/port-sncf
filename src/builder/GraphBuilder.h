#pragma once
#include "InstanceData.h"
#include "TimeSpaceGraph.h"




class GraphBuilder {
private:
    const InstanceData& data_;
    std::vector<Node> temp_nodes_;
    std::vector<Arc> temp_arcs_;
    std::vector<std::vector<int>> temp_arcs_providing_services; // for a given service id gives back the arcs providing the service
    std::vector<std::vector<bool>> temp_arcs_accessible_by_train; // for a given train id give back a vector of boolean representing all the arcs accessible by a train
    std::vector<int> temp_dummy_arcs_; // for a given train id gives back the id of its dummy arc.
    int T_;
    int t_s_; 
    const int n_pnodes_;
    const int n_services_;
    int a_id{0}; //Global arc index ie number of already constructed arcs
    bool link_conflicts_;
    std::vector<int> link_resource_;          // for each PLink: its conflict resource id (or -1)
    std::vector<std::string> resource_names_; // names of all conflict resources
    void compute_link_resources();
    
    void compute_time_step();
    void build_nodes();

    void build_a_d_arcs();

    void helper_build_shunting_arc(int p_id, int q_id, int l, int t1, int d, int link_resource);
    void build_shunting_arcs();

    void helper_build_service_arc(int p_id, int l, int t1, int d, int s);
    void build_service_arcs();

    void helper_build_storage_arc(int p_id, int l, int t1, int d, int ttype);
    void build_storage_arcs();

    void helper_build_dwelling_arc(int p_id, int t1);
    void build_dwelling_arcs();

    void helper_build_state_transfer_arc(int p_id, int l, int t1);
    void build_state_transfer_arcs();

    void helper_build_dummy_arc_k(int k, int w_shu);
    void build_dummy_arcs();

public:
    // link_conflicts: one capacity-1 resource per physical link {p,q} and time step,
    // used by shunting moves in BOTH directions (forbids two trains crossing a link
    // in opposite directions at the same time). false = original model.
    explicit GraphBuilder(const InstanceData& instance_data, bool link_conflicts = true);
    const TimeSpaceGraph build();
};



