#include "PLink.h"


using namespace std;

// constructor
PLink::PLink(int id, int o, int d, int l):ID(id),origin(o),destination(d),length(l)
{

}



// getters
int PLink::get_ID() const
{
    return ID;
}
int PLink::get_origin() const
{
    return origin;
}
int PLink::get_destination() const
{
    return destination;
}
int PLink::get_length() const
{
    return length;
}
