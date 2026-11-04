#pragma once

#include <vector>
#include <utility>

enum ArcType
{
    ARRIVAL,
    DEPARTURE,
    SHUNTING,
    SERVICE,
    STORAGE,
    DWELLING,
    STATE_TRANSFER,
    DUMMY,
    DEFAULT
};

/*
Time space Arc class
Represents an arc in the virtual time-space network.
Contains an ID, type, from/to node indices, and costs per train.
*/
class Arc
{
private:
    int ID{0};
    ArcType type{DEFAULT};
    int from{0};
    int start_time{0}; 
    int end_time{0};
    int to{0};
    int cost{0};
    std::vector<int> cost_by_train{};
    int s{0};              // id of associated service if necessary, 0 otherwise
    std::vector<std::pair<int, int>> iNodes{}; // nodes for which arc is in incompatible node set

public:
    Arc(int id, ArcType ty, int o, int d, int w, int c, int t, int start_time, int end_time); // for all arcs
    Arc(int id, ArcType ty, int o, int d, int start_time, int end_time);                      // for cost 0 arcs
    Arc() = default;

    void add_node(int p, int t) { iNodes.push_back({p, t}); }
    void set_service(int s_id) { s = s_id; }
    void set_costs(const std::vector<int> &costs) { cost_by_train = costs; }

    int get_id() const { return ID; }
    int get_from() const { return from; }
    int get_to() const { return to; }
    int get_cost(int k) const { return cost_by_train.empty() ? cost : cost_by_train.at(k); }
    int get_service() const { return s; }
    int get_start_time() const {return start_time;}
    int get_end_time() const {return end_time;}
    ArcType get_type() const { return type; }
    const std::vector<std::pair<int, int>>& get_iNodes() const { return iNodes; }
};
