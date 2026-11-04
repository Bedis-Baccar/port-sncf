#pragma once
#include <vector>

/*
Time space network Node class
This class represents a node in the time space network.
It contains information about the node's ID, the physical ID (which physical node it refers to),
time coordinate and layer coordinate
*/
class Node
{
private:
    int ID{0};            // ID of the virtual node
    int p_ID{0};          // ID of the physical node
    int time{0};          // Time coordinate of the node
    int layer{100};         // Layer of the node in the time space network
    std::vector<int> iArcs{}; // vector of incompatible arcs for the node
    std::vector<int> fArcs{}; // that leave from this node
    std::vector<int> tArcs{}; // arcs that reach this node
public:
    Node(int id, int p, int t, int l);
    Node() = default;

    void addIncArc(int a) { iArcs.push_back(a); }
    void addFromArc(int a) { fArcs.push_back(a); }
    void addToArc(int a) { tArcs.push_back(a); }

    int getPNode() const;
    int getTime() const;
    int getID() const {return ID;};
    const std::vector<int>& getIncArcs() const { return iArcs; }
    const std::vector<int>& getFromArcs() const { return fArcs; }
    const std::vector<int>& getToArcs() const { return tArcs; }
};


