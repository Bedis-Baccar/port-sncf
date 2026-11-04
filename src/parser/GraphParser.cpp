#include "GraphParser.h"
#include <vector>
#include <iostream>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <string>
#include <algorithm>

using namespace std;

InstanceParser::InstanceParser(const string& directory_path): directory_path_(directory_path)
{

}

vector<vector<string>> InstanceParser::readCSV(const string& filepath) const{
    vector<vector<string>> data;
    ifstream file(filepath);

    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + filepath + "\n");
    }

    string line;
    while (getline(file, line))
    {
        vector<string> row;
        stringstream ss(line);
        string cell;

        while (getline(ss, cell, ','))
        {
            row.push_back(cell);
        }

        data.push_back(row);
    }
    file.close();
    return data;
}

void InstanceParser::parse_services(){
   
    vector<vector<string>> data;
    string filepath = directory_path_ + "Service input.csv";
    data = readCSV(filepath);
    int n = data.size();
    int id;

    if (n < 1 || data[0].size() != 4) {
        throw std::runtime_error("Wrong input file format, empty file or wrong number of columns: " + filepath + 
                                "\nNumber of columns: " + std::to_string(data[0].size()) + 
                                "\nExpected number of columns: 4\n");
    }

    for (int i = 1; i < n; i++)
    {
        id = stoi(data[i][0]);
        if (i != id) {
             throw std::runtime_error("Invalid input file, service id invalid: " + filepath + 
                                    "\nService ID: " + std::to_string(i) + 
                                    ", Wrong given ID: " + std::to_string(id) + "The two should match check the instance files\n");
        }
        parsed_data_.services.push_back(Service(id - 1, data[i][1], stoi(data[i][2]), stoi(data[i][3])));
    }
    n_services_ = n - 1;
    return;
}

void InstanceParser::parse_node_types(){
    string filepath = directory_path_+"../Node types.csv";
    vector<vector<string>> data;
    data = readCSV(filepath);
    int n = data.size();
    int id; // stores node type id

    if (n < 1 || static_cast<int>(data[0].size()) != 5 + n_services_) {
        throw std::runtime_error("Wrong input file format, empty file or wrong number of columns based on given services: " + filepath + 
                                "\nNumber of columns: " + std::to_string(data[0].size()) + 
                                "\nExpected number of columns: " + std::to_string(5 + n_services_) + "\n");
    }
    for (int i = 1; i < n; i++)
    {
        id = stoi(data[i][0]);
        if (i != id) {
            throw std::runtime_error("Invalid input file, node type id invalid: " + filepath + 
                                    "\nNode type ID: " + std::to_string(i) + 
                                    ", Wrong given ID: " + std::to_string(id) + " The two should match check instance files\n");
        }
        parsed_data_.node_types.push_back(NodeType(id - 1, data[i][1], data[i][2] == "1", data[i][3] == "1", data[i][4] == "1", {}));
        for (int j = 0; j < n_services_; j++)
        {
            parsed_data_.node_types[i - 1].addService(stoi(data[i][5 + j]) == 1); // For some reason doesn't work for the last column when using =="1" so transforming to int first
        }
    }
    n_node_types_ = n - 1;
    return;

}


void InstanceParser::parse_nodes(){
    vector<vector<string>> data;
    string filepath = directory_path_ + "../Node input.csv";
    data = readCSV(filepath);
    int n = data.size();
    int type; // stores node type
    int id;

    if (n < 1 || data[0].size() != 3) {
        throw std::runtime_error("Wrong input file format, empty file or wrong number of columns: " + filepath + 
                                "\nNumber of columns: " + std::to_string(data[0].size()) + 
                                "\nExpected number of columns: 3\n");
    }

    for (int i = 1; i < n; i++)
    {
        id = stoi(data[i][0]);
        if (i != id) {
            throw std::runtime_error("Invalid input file, node id invalid: " + filepath + 
                                    "\nNode ID: " + std::to_string(i) + 
                                    ", Wrong given ID: " + std::to_string(id) + " The two should match check instance file\n");
        }
        type = stoi(data[i][2]);
        if (type <= 0 || type > n_node_types_) {
            throw std::runtime_error("Invalid input file, node type invalid: " + filepath + 
                                    "\nNode ID: " + std::to_string(id) + 
                                    ", Node type: " + std::to_string(type) + "\n");
        }
        parsed_data_.pnodes.push_back(PNode(id - 1, data[i][1], type - 1));
        parsed_data_.node_types[type - 1].addNode(id - 1);
    }
    n_nodes_ = n - 1;
    return; 
}

void InstanceParser::parse_links(){
    string filepath = directory_path_ + "../Link input.csv";
    vector<vector<string>> data;
    data = readCSV(filepath);
    int n = data.size();
    int o;  // stores origin id
    int d;  // stores destination id
    int id; // stores link id

    if (n < 1 || data[0].size() != 4) {
        throw std::runtime_error("Wrong input file format, empty file or wrong number of columns: " + filepath + 
                                "\nNumber of columns: " + std::to_string(data[0].size()) + 
                                "\nExpected number of columns: 4\n");
    }

    for (int i = 1; i < n; i++)
    {
        id = stoi(data[i][0]);
        if (i != id) {
            throw std::runtime_error("Invalid input file, link id invalid: " + filepath + 
                                    "\nLink ID: " + std::to_string(i) + 
                                    ", Wrong given ID: " + std::to_string(id) + " The two should match check instance files\n");
        }
        o = stoi(data[i][1]);
        d = stoi(data[i][2]);
        parsed_data_.plinks.push_back(PLink(id - 1, o - 1, d - 1, stoi(data[i][3])));

        if (o < 1 || o > n_nodes_ || d < 1 || d > n_nodes_) {
            throw std::runtime_error("Invalid input file, origin or destination nodes invalid: " + filepath + 
                                    "\nLink ID: " + std::to_string(id) + 
                                    ", Origin: " + std::to_string(o) + 
                                    ", Destination: " + std::to_string(d) + 
                                    "\nNumber of nodes: " + std::to_string(n_nodes_) + "\n");
        }
    }
    n_links_ = n - 1;
    return;
}

void InstanceParser::parse_train_types(){
    string filepath = directory_path_ + "Train types.csv";
    vector<vector<string>> data;
    data = readCSV(filepath);
    int n = data.size();
    int id; // stores train type id

    if (n < 1 || data[0].size() != 4) {
        throw std::runtime_error("Wrong input file format, empty file or wrong number of columns: " + filepath + 
                                "\nNumber of columns: " + std::to_string(data[0].size()) + 
                                "\nExpected number of columns: 4\n");
    }

    for (int i = 1; i < n; i++)
    {
        id = stoi(data[i][0]);
        if (i != id) {
            throw std::runtime_error("Invalid input file, train type id invalid: " + filepath + 
                                    "\nTrain type ID: " + std::to_string(i) + 
                                    ", Wrong given ID: " + std::to_string(id) + " The two should match check instance files\n");
        }
        parsed_data_.train_types.push_back(TrainType(id - 1, data[i][1], stoi(data[i][2]), stoi(data[i][3])));
    }
    n_train_types_ = n - 1;
    return; 
}

void InstanceParser::parse_trains(){
    string filepath = directory_path_ + "Train input.csv";
    vector<vector<string>> data;
    data = readCSV(filepath);
    int n = data.size();
    int type; // stores train type
    int id;   // stores train id
    int a;    // stores arrival time
    int d;    // stores departure time

    if (n < 1 || data[0].size() != 5) {
        throw std::runtime_error("Wrong input file format, empty file or wrong number of columns: " + filepath + 
                                "\nNumber of columns: " + std::to_string(data[0].size()) + 
                                "\nExpected number of columns: 5\n");
    }

    for (int i = 1; i < n; i++)
    {
        if (data[i].size() != 5) {
            throw std::runtime_error("Wrong number of columns: " + filepath + " line " + std::to_string(i) + 
                                    "\nNumber of columns: " + std::to_string(data[i].size()) + 
                                    "\nExpected number of columns: 5\n");
        }
        id = stoi(data[i][0]);
        if (i != id) {
            throw std::runtime_error("Invalid input file, train id invalid: " + filepath + 
                                    "\nTrain ID: " + std::to_string(i) + 
                                    ", Wrong given ID: " + std::to_string(id) + "\n");
        }
        type = stoi(data[i][2]);
        if (type <= 0 || type > n_train_types_) {
            throw std::runtime_error("Invalid input file, train type invalid: " + filepath + 
                                    "\nTrain ID: " + std::to_string(id) + 
                                    ", Wrong given type: " + std::to_string(type) + 
                                    "\nNumber of types: " + std::to_string(n_train_types_) + "\n");
        }
        a = stoi(data[i][3]);
        d = stoi(data[i][4]);
        if (a < 0 || d < a) {
            throw std::runtime_error("Invalid input file, arrival or departure time invalid: " + filepath + 
                                    "\nTrain ID: " + std::to_string(id) + 
                                    ", Arrival time: " + std::to_string(a) + 
                                    ", Departure time: " + std::to_string(d) + "\n");
        }
        parsed_data_.trains.push_back(Train(id - 1, data[i][1], type - 1, stoi(data[i][3]), stoi(data[i][4]), n_services_));
    }
    n_trains_ = n - 1;
    return;
}


void InstanceParser::parse_operations(){
    string filepath = directory_path_ + "Operations assignment.csv";
    vector<vector<string>> data;
    data = readCSV(filepath);
    int n = data.size();
    int train;                            // stores train id
    int type;                          // stores service type
    parsed_data_.assignement = vector<int>(n_services_, 0); // stores number of times a given service is assigned

    if (n < 1 || data[0].size() != 3) {
        throw std::runtime_error("Wrong input file format, empty file or wrong number of columns: " + filepath + 
                                "\nNumber of columns: " + std::to_string(data[0].size()) + 
                                "\nExpected number of columns: 3\n");
    }
    for (int i = 1; i < n; i++)
    {
        type = stoi(data[i][1]);
        train = stoi(data[i][2]);
        if (train <= 0 || train > n_trains_ || type <= 0 || type > n_services_) {
            throw std::runtime_error("Invalid input file, train or service type invalid: " + filepath + 
                                    "\nOperation ID: " + data[i][0] + 
                                    "\nTrain ID: " + std::to_string(train) + 
                                    ", Service type: " + std::to_string(type) + 
                                    "\nNumber of trains: " + std::to_string(n_trains_) + 
                                    ", Number of services: " + std::to_string(n_services_) + "\n");
        }
        if (parsed_data_.trains[train - 1].get_service(type - 1)) {
            throw std::runtime_error("Invalid input file, service assigned twice or more to the same train: " + filepath + 
                                    "\nOperation ID: " + data[i][0] + 
                                    "\nTrain ID: " + std::to_string(train) + 
                                    ", Service type: " + std::to_string(type) + 
                                    "\nNumber of trains: " + std::to_string(n_trains_) + 
                                    ", Number of services: " + std::to_string(n_services_) + "\n");
        }
        parsed_data_.trains[train - 1].set_service(type - 1);
        parsed_data_.assignement[type - 1]++;
    }
    return;
}


InstanceData InstanceParser::parse(){
    parse_services();
    parse_node_types();
    parse_nodes();
    parse_links();
    parse_train_types();
    parse_trains();
    parse_operations();
    return parsed_data_;
}