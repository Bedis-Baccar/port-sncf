#pragma once

#include <string>
#include <vector>


/*
Service class
Contains all necessary information to a given service type.
Their id, name, fixed cost and duration
*/
struct Service
{
private:
    int ID = -1;
    std::string name = "default";
    int cost = 0;
    int duration = 0;

public:
    Service(int id, std::string n, int c, int d);
    Service() = default;


    int getID() const;
    std::string getName() const;
    int getCost() const;
    int getDuration() const;
};
