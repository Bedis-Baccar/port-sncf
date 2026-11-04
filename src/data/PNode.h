#pragma once
#include <string>
#include <vector>

/*
Physical Node class
This class represents a physical node in the network.
It contains information about the node's ID, name, and type.
*/
class PNode
{
private:
    int ID = 0;
    std::string name = "default";
    int type = 0;

public:
    PNode(int id, std::string n, int t);

    int getID();
    std::string getName();
    int getType();
};

/*
Node type class
Contains all necessary information to a node type and allowed services
id, name, io, parking, dwelling, services and a list of nodes of its type
*/
class NodeType
{
private:
    int ID = 0;
    std::string name = "default";
    bool io = false;
    bool parking = false;
    bool dwelling = false;
    std::vector<bool> services = {};
    std::vector<int> pnodes = {};

public:
    NodeType(int id, std::string n, bool i, bool p, bool d, std::vector<bool> s);
    NodeType() = default;

    void addService(bool s) { services.push_back(s); }
    void addNode(int p);

    int getID();
    std::string getName() const;
    bool getIO() const;
    bool getParking() const;
    bool getDwelling() const;
    const std::vector<bool>& getServices() const { return services; }
    bool getService(int i) const;
    const std::vector<int>& getNodes() const{ return pnodes; }
};


