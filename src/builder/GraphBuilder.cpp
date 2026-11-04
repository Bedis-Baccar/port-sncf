#include "GraphBuilder.h"
#include "TimeSpaceGraph.h"
#include "Node.h"
#include "Train.h"
#include "Service.h"
#include "PLink.h"
#include "param.h"
#include <algorithm>

void GraphBuilder::compute_time_step()
{
    int t = 0; // time step
    int T = 0;
    for (Train train : data_.trains)
    {
        if(train.get_d_t() > T){
            T = train.get_d_t();
        }
        t = __gcd(t, train.get_a_t());
        t = __gcd(t, train.get_d_t());
    }
    for (Service service : data_.services)
    {
        t = __gcd(t, service.getDuration());
    }
    for (TrainType train_type : data_.train_types)
    {
        t = __gcd(t, train_type.get_t_turn());
    }
    for (PLink link : data_.plinks)
    {
        t = __gcd(t, link.get_length());
    }
    t_s_ =  t;
    T_ = T;  
}

void GraphBuilder::build_nodes()
{
    
    temp_nodes_.push_back(Node(0, -1, 0, 1));
    temp_nodes_.push_back(Node(1, -1, T_, 2));
    int id = 1;
    for (int t = 0; t <= T_; t += t_s_)
    {
        for (int p = 0; p < n_pnodes_; p++)
        {
            for (int l = 0; l < 3; l++)
            {
                id += 1;
                temp_nodes_.push_back(Node(id, p, t, l));
            }
        }
    }
}



void GraphBuilder::build_a_d_arcs()
{
    a_id = 0; //Security to make sure the global arc index a_id is set at 0
    int a_t;
    int d_t;
    for (auto& t : data_.trains){
        a_t = t.get_a_t();
        d_t = t.get_d_t();
        int v;
        for (auto& type: data_.node_types)
        {
            if (type.getIO())
            {
                for (int p : type.getNodes())
                {
                    v = TimeSpaceGraph::compute_virtual_node_id(p, a_t, 1,t_s_,n_pnodes_);
                    temp_arcs_.push_back(Arc(a_id, ARRIVAL, 0, v, 0, a_t));
                    temp_nodes_[0].addFromArc(a_id);
                    temp_nodes_[v].addToArc(a_id);
                    a_id++;
                    for (auto& t2 : data_.trains){
                        temp_arcs_accessible_by_train[t2.get_ID()].push_back(t.get_ID() == t2.get_ID());
                    }

                    v = TimeSpaceGraph::compute_virtual_node_id(p , d_t, 2, t_s_, n_pnodes_);
                    temp_arcs_.push_back(Arc(a_id, DEPARTURE, v, 1, d_t, T_));


                    temp_nodes_[v].addFromArc(a_id);
                    temp_nodes_[1].addToArc(a_id);

                    for (int t = 0; t < min(t_h, T_ - d_t + 1); t += t_s_)
                    { // add incompatible arc to each corresponding node during the safety headway interval
                        v = TimeSpaceGraph::compute_virtual_node_id(p, d_t + t, 0, t_s_, n_pnodes_);
                        temp_nodes_[v].addIncArc(a_id);
                        temp_arcs_[a_id].add_node(p, d_t + t);
                    }
                    a_id++;
                    for (auto& t2 : data_.trains){
                        temp_arcs_accessible_by_train[t2.get_ID()].push_back(t.get_ID() == t2.get_ID());
                    }
                }
            }
        }
    }
}


void GraphBuilder::helper_build_shunting_arc(int p_id, int q_id, int l, int t1, int d)
{
    int n1 = TimeSpaceGraph::compute_virtual_node_id(p_id, t1, l, t_s_, n_pnodes_);
    int n2 = TimeSpaceGraph::compute_virtual_node_id(q_id, t1 + d, l, t_s_, n_pnodes_);
    int v;
    temp_arcs_.push_back(Arc(a_id, SHUNTING, n1, n2, w_shu, c_shu, d, t1, t1 + d));
    vector<int> costs;
    costs.reserve(data_.trains.size());
    for (const Train &train : data_.trains)
    {
        int w = data_.train_types[train.get_type()].get_w_shu();
        costs.push_back(w * d + c_shu);
    }
    temp_arcs_[a_id].set_costs(costs);
    temp_nodes_[n2].addToArc(a_id);
    temp_nodes_[n1].addFromArc(a_id);
    for (int dt = 0; dt < min(t_h, T_ - t1 + 1); dt += t_s_)
    { // add incompatible arc to each corresponding departure node during the safety headway interval
        v = TimeSpaceGraph::compute_virtual_node_id(p_id, t1 + dt, 0, t_s_, n_pnodes_);
        temp_nodes_[v].addIncArc(a_id);
        temp_arcs_[a_id].add_node(p_id, dt + t1);
    }
    a_id++;
    for(int k = 0; k < data_.trains.size(); k++){
        temp_arcs_accessible_by_train[k].push_back(true);
    }

}

void GraphBuilder::build_shunting_arcs()
{
    int p_id;
    int q_id;
    int l;
    int d; // shunting duration

    for (const PLink& link : data_.plinks)
    {
        d = link.get_length();
        p_id = link.get_origin();
        q_id = link.get_destination();
        for (int t1 = 0; t1 < T_; t1 += t_s_)
        {
            if (t1 + d > T_)
            {
                break;
            }
            // Layer 1 arc
            l = 1;
            helper_build_shunting_arc(p_id, q_id, l, t1, d);
            // Layer 2 arc
            l = 2;
            helper_build_shunting_arc(q_id, p_id, l, t1, d);
        }
    }
}

void GraphBuilder::helper_build_service_arc(int p_id, int l, int t1, int d, int s)
{
    int n1 = TimeSpaceGraph::compute_virtual_node_id(p_id, t1, l, t_s_, n_pnodes_);
    int n2 = TimeSpaceGraph::compute_virtual_node_id(p_id, t1 + d, 0, t_s_, n_pnodes_);
    int v;
    int c = data_.services[s].getCost();

    temp_arcs_.push_back(Arc(a_id, SERVICE, n1, n2, 0, c, d, t1, t1 + d)); 
    temp_arcs_[a_id].set_service(s);
    temp_arcs_providing_services[s].push_back(a_id);

    temp_nodes_[n2].addToArc(a_id);
    temp_nodes_[n1].addFromArc(a_id);
    for (int dt = 0; dt < d; dt += t_s_)
    { // add incompatible arc to each node it occupies during service
        v = TimeSpaceGraph::compute_virtual_node_id(p_id, t1 + dt, 0, t_s_, n_pnodes_);
        temp_nodes_[v].addIncArc(a_id);
        temp_arcs_[a_id].add_node(p_id, t1 + dt);
    }
    a_id++;
    for(auto& train : data_.trains){
        temp_arcs_accessible_by_train[train.get_ID()].push_back(train.get_service(s));
    }
}

void GraphBuilder::build_service_arcs()
{
    int d;
    int w = 0;
    for (auto& service : data_.services) // For each service...
    {
        d = service.getDuration();
        for (auto& type : data_.node_types) // if the node authorizes this service...
        {
            if (type.getService(service.getID()))
            {
                for (int p_id : type.getNodes())
                {
                    for (int l = 1; l < 3; l++) // for each potential starting layer...
                    {
                        for (int t1 = 0; t1 <= T_ - d; t1 += t_s_) // for each time instant.
                        {
                            helper_build_service_arc(p_id, l, t1, d, service.getID());
                        }
                    }
                }
            }
        }
    }
}

void GraphBuilder::helper_build_storage_arc(int p_id, int l, int t1, int d, int ttype)
{
    int n1 = TimeSpaceGraph::compute_virtual_node_id(p_id, t1, l, t_s_, n_pnodes_);
    int n2 = TimeSpaceGraph::compute_virtual_node_id(p_id, t1 + d, 0, t_s_, n_pnodes_);
    int v;

    temp_arcs_.push_back(Arc(a_id, STORAGE, n1, n2, w_dwell, c_park, d, t1, t1 + d)); 

    temp_nodes_[n2].addToArc(a_id);
    temp_nodes_[n1].addFromArc(a_id);
    for (int dt = 0; dt < d; dt += t_s_)
    { // add incompatible arc to each node it occupies during service
        v = TimeSpaceGraph::compute_virtual_node_id(p_id, t1 + dt, 0, t_s_, n_pnodes_);
        temp_nodes_[v].addIncArc(a_id);
        temp_arcs_[a_id].add_node(p_id, dt + t1);
    }
    for (auto& train : data_.trains) // Add arcs to train arcs
    {
        temp_arcs_accessible_by_train[train.get_ID()].push_back(train.get_type() == ttype);
    }
    a_id++;
}

void GraphBuilder::build_storage_arcs()
{
    int d;

    for (auto& ntype : data_.node_types)
    {
        if (ntype.getParking())
        {
            for (auto& ttype : data_.train_types)
            {
                d = ttype.get_t_turn();
                for (int p_id : ntype.getNodes())
                {
                    for (int l = 1; l < 3; l++)
                    {
                        for (int t1 = 0; t1 <= T_ - d; t1 += t_s_)
                        {
                            helper_build_storage_arc(p_id, l, t1, d, ttype.get_ID());
                        }
                    }
                }
            }
        }
    }
}

void GraphBuilder::helper_build_dwelling_arc(int p_id, int t1)
{
    int n1 = TimeSpaceGraph::compute_virtual_node_id(p_id, t1, 0, t_s_, n_pnodes_);
    int n2 = TimeSpaceGraph::compute_virtual_node_id(p_id, t1 + t_s_, 0, t_s_, n_pnodes_);

    temp_arcs_.push_back(Arc(a_id, DWELLING, n1, n2, w_dwell, 0, t_s_, t1, t1 + t_s_));
    temp_nodes_[n2].addToArc(a_id);
    temp_nodes_[n1].addFromArc(a_id);

    temp_nodes_[n1].addIncArc(a_id);
    temp_arcs_[a_id].add_node(p_id, t1);
    for (auto &train : data_.trains)
    {
        temp_arcs_accessible_by_train[train.get_ID()].push_back(true);
    }
    a_id++;
}

void GraphBuilder::build_dwelling_arcs()
{
    for (auto& ntype : data_.node_types)
    {
        if (ntype.getDwelling())
        {
            for (int p_id : ntype.getNodes())
            {
                for (int t1 = 0; t1 < T_; t1 += t_s_)
                {
                    helper_build_dwelling_arc(p_id, t1);
                }
            }
        }
    }
}

void GraphBuilder::helper_build_state_transfer_arc(int p_id, int l, int t1)
{
    int n1 = TimeSpaceGraph::compute_virtual_node_id(p_id, t1, 0, t_s_, n_pnodes_);
    int n2 = TimeSpaceGraph::compute_virtual_node_id(p_id, t1, l, t_s_, n_pnodes_);

    temp_arcs_.push_back(Arc(a_id, STATE_TRANSFER, n1, n2, t1, t1));
    temp_nodes_[n2].addToArc(a_id);
    temp_nodes_[n1].addFromArc(a_id);
    for (auto& train : data_.trains)
    {
        temp_arcs_accessible_by_train[train.get_ID()].push_back(true);
    }
    a_id++;
}

void GraphBuilder::build_state_transfer_arcs()
{
    bool can_0; // stores wether a type can enter dwelling layer
    for (auto& ntype : data_.node_types)
    {
        can_0 = ntype.getParking();
        for (int s = 1; s <= n_services_; s++)
        {
            can_0 = can_0 || ntype.getService(s);
        }
        if (can_0)
        {
            for (int p_id : ntype.getNodes()){
                for (int l = 1; l < 3; l++)
                {
                    for (int t1 = 0; t1 < T_; t1 += t_s_)
                    {
                        helper_build_state_transfer_arc(p_id, l, t1);

                    }
                }
            }
        }
    }
}

void GraphBuilder::helper_build_dummy_arc_k(int k, int w_shu)
{
    int d = data_.trains[k].get_d_t() - data_.trains[k].get_a_t();
    int w = w_shu + w_dwell + c_park + c_shu;
    int c = 0;

    for (auto& s : data_.services)
    {
        c += s.getCost();
    }

    c = 2 * c;

    temp_arcs_.push_back(Arc(a_id, DUMMY, 0, 1, w, c, d, 0, T_));
    temp_dummy_arcs_[k] = a_id;
    for (auto& train : data_.trains)
    {
        temp_arcs_accessible_by_train[train.get_ID()].push_back(train.get_ID() == k);
    }
    a_id++;
}

void GraphBuilder::build_dummy_arcs()
{
    int w_shu;
    for (auto& train : data_.trains)
    {
        w_shu = data_.train_types[train.get_type()].get_w_shu();
        helper_build_dummy_arc_k(train.get_ID(), w_shu);
    }
}


GraphBuilder::GraphBuilder(const InstanceData& data):data_(data),n_services_(data.services.size()),n_pnodes_(data.pnodes.size())
{
    temp_nodes_ = {};
    temp_arcs_ = {};
    temp_arcs_providing_services = vector<vector<int>>(n_services_);
    temp_arcs_accessible_by_train = vector<vector<bool>>(data_.trains.size());
    temp_dummy_arcs_ = vector<int>(data.trains.size());

}

const TimeSpaceGraph GraphBuilder::build(){
    compute_time_step();
    build_nodes();
    build_a_d_arcs();
    build_shunting_arcs();
    build_service_arcs();
    build_storage_arcs();
    build_dwelling_arcs();
    build_state_transfer_arcs();
    build_dummy_arcs();

    return TimeSpaceGraph(T_, t_s_, n_pnodes_, n_services_, temp_nodes_, temp_arcs_, temp_arcs_providing_services, temp_arcs_accessible_by_train, temp_dummy_arcs_);
}

