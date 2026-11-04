#include "Train.h"
using namespace std;

Train::Train(int id, string n, int t, int a, int b, int ns):ID(id),name(n),type(t),a_t(a),d_t(b), nb_services_required_(0)
{
    for (int s = 0; s < ns; s++)
    {
        services.push_back(false);
    }
}


int Train::get_ID() const
{
    return ID;
}
string Train::get_name() const
{
    return name;
}
int Train::get_type() const
{
    return type;
}
int Train::get_a_t() const
{
    return a_t;
}
int Train::get_d_t() const
{
    return d_t;
}
vector<bool> Train::get_services() const
{
    return services;
}

void Train::set_service(int s){
    if(services[s]){
        return;
    }
    services[s] = true;
    nb_services_required_++;
}

TrainType::TrainType(int id, string n, int w, int t):ID(id),name(n),w_shu(w),t_turn(t)
{

}


int TrainType::get_ID() const
{
    return ID;
}
string TrainType::get_name() const
{
    return name;
}
int TrainType::get_w_shu() const
{
    return w_shu;
}
int TrainType::get_t_turn() const
{
    return t_turn;
}
