#ifndef TRAIN_H
#define TRAIN_H

#include <string>
#include <vector>
using namespace std;

/*
Train class
This class represents a train in the network.
It contains information about the train's ID, name, type, arrival time, departure time
and the services needed to be performed.
*/
class Train
{
private:
    int ID{0};
    string name{"default"};
    int type{0};
    int a_t{0};
    int d_t{0};
    vector<bool> services{};
    int nb_services_required_; 

public:
    Train(int id, string n, int t, int a, int d, int ns);
    Train() = default;

    void set_service(int s);
    int get_ID() const;
    string get_name() const;
    int get_type() const;
    int get_a_t() const;
    int get_d_t() const;

    vector<bool> get_services() const;
    bool get_service(int s) const { return services[s]; }
    int get_nb_services_required() const {return nb_services_required_;}
};

/*
Train_Type class
This class represents a train type in the network.
It contains information about the train type's ID, name, shunting weight and turnaround time.
*/
class TrainType
{
private:
    int ID{0};
    string name = "default";
    int w_shu = 0;
    int t_turn = 0;

public:
    TrainType(int id, string n, int w, int t);
    TrainType() = default;

    int get_ID() const;
    string get_name() const;
    int get_w_shu() const;
    int get_t_turn() const;
};

#endif
