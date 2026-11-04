#include "PNode.h"
#include <stdexcept>
using namespace std;

// constructor
PNode::PNode(int id, string n, int t):ID(id),name(n),type(t)
{
}


// getters
int PNode::getID()
{
    return ID;
}
string PNode::getName()
{
    return name;
}
int PNode::getType()
{
    return type;
}

NodeType::NodeType(int id, string n, bool i, bool p, bool d, vector<bool> s):ID(id),name(n),io(i),parking(p),dwelling(d),services(s)
{
    pnodes = {};
}


// print node type information
void NodeType::addNode(int p)
{
    pnodes.push_back(p);
}

// getters
int NodeType::getID()
{
    return ID;
}
string NodeType::getName() const
{
    return name;
}
bool NodeType::getIO() const
{
    return io;
}
bool NodeType::getParking() const
{
    return parking;
}
bool NodeType::getDwelling() const
{
    return dwelling;
}
bool NodeType::getService(int i) const
{
    if (i < 0 || i > static_cast<int>(services.size()))
    {
        throw std::runtime_error("Error index out of bound index for a service, index queried was " + to_string(i) 
                                + "The bound are " + to_string(0) + ", " + to_string(static_cast<int>(services.size())));
    }
    return services[i];
}
