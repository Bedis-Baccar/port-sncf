#include "TimeSpaceGraph.h"
#include "Node.h"
#include "Arc.h"
#include <vector>

using namespace std;

TimeSpaceGraph::TimeSpaceGraph(int time_horizon, int time_step, int num_physical_nodes, int num_services, std::vector<Node> nodes, std::vector<Arc> arcs, std::vector<std::vector<int>> arc_providing_services, std::vector<std::vector<bool>> arc_accessible_by_train, std::vector<int> dummy_arcs):
t_s_(time_step),
T_(time_horizon),
num_pnodes_(num_physical_nodes),
nodes_(nodes),
arcs_(arcs),
arc_providing_services_(arc_providing_services),
arc_accessible_by_train_(arc_accessible_by_train),
dummy_arcs_(dummy_arcs),
num_services_(num_services)
{}

void TimeSpaceGraph::add_node(const Node& node){
    nodes_.push_back(node);
}

void TimeSpaceGraph::add_arc(const Arc& arc){
    arcs_.push_back(arc);
}


const vector<Node>& TimeSpaceGraph::get_nodes() const{
    return nodes_;
}

const std::vector<Arc>& TimeSpaceGraph::get_arcs() const{
    return arcs_;
}

const Node& TimeSpaceGraph::get_node(int id) const{
    return nodes_.at(id);
}

const Arc& TimeSpaceGraph::get_arc(int id) const{
    return arcs_.at(id);
}

int TimeSpaceGraph::compute_virtual_node_id(int p, int t, int l) const{
    if(p == -1){ //Traitement du noeud source puit fictif 
        return l-1;
    }
    return p  * 3 + (t / t_s_) * num_pnodes_ * 3 + l + 2;
}

int TimeSpaceGraph::compute_virtual_node_id(int p, int t, int l, int time_step, int num_pnodes) {
    if(p == -1){ //Traitement du noeud source puit fictif 
        return l-1;
    }
    return p * 3 + (t / time_step) * num_pnodes * 3 + l + 2;
}

const std::vector<int>& TimeSpaceGraph::get_arcs_providing_service(int service_id) const{
    return arc_providing_services_[service_id];
}

