#include "Node.h"
using namespace std;
Node::Node(int id, int p, int t, int l):ID(id),p_ID(p),time(t),layer(l)
{
}


int Node::getPNode() const
{
    return p_ID;
}
int Node::getTime() const
{
    return time;
}
