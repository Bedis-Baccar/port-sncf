#pragma once

using namespace std;

/*
Physical Link class
This class represents a physical link in the network.
It contains information about the link's ID, origin, destination and length.
*/
class PLink
{
private:
    int ID = 0;
    int origin = 0;
    int destination = 0;
    int length = 0;

public:
    PLink(int id, int o, int d, int l);
    PLink() = default;
    int get_ID() const;
    int get_origin() const;
    int get_destination() const;
    int get_length() const;
};

