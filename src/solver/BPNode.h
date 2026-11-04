#pragma once

#include <tuple>
#include <vector>
#include <map>
#include <string>

enum class NodeStatus {
    UNPROCESSED, PROCESSING, FRACTIONAL, INTEGER, INFEASIBLE, PRUNED
};

struct BPNode {
    //Parent and children of the BPNode
    BPNode* parent;
    std::vector<BPNode*> children;
    //Node depth
    int depth;
    // Node lower bound - best lowerbound obtained on this node
    double lower_bound;
    //Status of the BP Node
    NodeStatus status;
    //Which trains the restriction are applied on
    std::vector<int> train_ids;
    // Subset of forbidden arcs decided in the parent node 
    std::map<int,std::vector<int>> forbidden_arcs_ids;
    // Unique id (creation order, root = 0) and description of the branching
    // decision that created the node. Used by the trace / web app.
    int id = -1;
    std::string label = "";
}
;

