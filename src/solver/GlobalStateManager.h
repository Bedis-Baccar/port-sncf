#pragma once
#include <vector>
#include "Column.h"
#include <cstdint>
#include <map>

/*
A class to deal with the physical/mathematical change on the graph/RMP dictated by the position on the search tree (decided by the Search Tree Object)
*/
using ColumnList = std::vector<int>;

class GlobalStateManager{
    public :
        GlobalStateManager(int num_trains, int num_arcs);
        void register_column(const Column& col); // To update the arc_to_columns vector and initialise its conflict count
        ColumnList apply_delta(const std::vector<int>& train_ids, const std::map<int, std::vector<int>>& forbidden_arcs);
        ColumnList revert_delta(const std::vector<int>& train_ids, const std::map<int, std::vector<int>>& forbidden_arcs);
        bool is_column_disabled(int column_id) const;
        bool is_arc_forbidden(int train_id, int arc_id) const;
    private :
        int num_trains_;
        int num_arcs_;
        
        std::vector<ColumnList> flat_arc_to_columns_; // gives back for   given train_id and a given arc in the TimeSpaceGraph the list of all the id of generated columns possessing this arc in their path
                                                    //Indexing is : train_id * num_arcs + arc_id
        std::vector<int> flat_is_forbidden_; // dimension, nb_trains * nb_arc. To access  num_arcs_ * train_id + arc_id
        std::vector<int> conflict_count_; // For each column contains its number of restriction (ie number of forbiden arc contained in the path)
        
        ColumnList apply_delta_single_train(int train_id, const std::vector<int>& forbidden_arcs); //Gives back the ids of the Columns to remove after applying restriction
        ColumnList revert_delta_single_train(int train_id, const std::vector<int>& forbidden_arcs);//Gives back the ids of the Columns to add back after removing restriction
};