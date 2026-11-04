#pragma once
#include "PLink.h"
#include "PNode.h"
#include "Service.h"
#include "Train.h"
#include <vector>


struct InstanceData {
    std::vector<PNode> pnodes;
    std::vector<NodeType> node_types;
    std::vector<PLink> plinks;
    std::vector<Train> trains;
    std::vector<TrainType> train_types;
    std::vector<Service> services;
    std::vector<int> assignement;
};