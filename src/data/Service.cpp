#include "Service.h"


using namespace std;

// Constructors
Service::Service(int id, string n, int c, int d):ID(id),name(n),cost(c),duration(d)
{
}


// Getters
int Service::getID() const
{
    return ID;
}
string Service::getName() const
{
    return name;
}
int Service::getCost() const
{
    return cost;
}
int Service::getDuration() const
{
    return duration;
}


