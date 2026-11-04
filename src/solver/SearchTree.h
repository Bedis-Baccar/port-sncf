#pragma once

#include <vector>
#include <queue>
#include <stack>
#include <map>
#include <memory>
#include "Column.h"
#include "BPNode.h"

// Comparateur pour la file de priorité (Best Bound First)
struct CompareNodeBound {
    bool operator()(const BPNode* a, const BPNode* b) const {
        return a->lower_bound > b->lower_bound;
    }
};

enum class SearchStrategy {
    DFS,
    BBF
};

//Trajectoire renvoyé au solveur pour qu'il fasse les modifications nécessaires 
struct Trajectory {
    std::vector<BPNode*> nodes_to_revert; // Les nœuds qu'il faut remonter (annuler)
    std::vector<BPNode*> nodes_to_apply;  // Les nœuds qu'il faut descendre (appliquer)
};

class SearchTree {
public:
    SearchTree();
    SearchTree(const std::vector<Column>& incumbent_solution, double value);
    
    // --- Navigation dans l'arbre ---
    BPNode* get_next_node();
    void set_strategy_DFS();  
    void set_strategy_BBF();
    Trajectory trajectory_to_node(BPNode* target_node);
    
    // --- Construction de l'arbre ---
    // Crée un nœud enfant, le lie à son parent, gère sa mémoire et l'ajoute aux files d'attente
    BPNode* create_child_node(BPNode* parent, const std::vector<int>& train_id, const std::map<int,std::vector<int>>& additional_forbidden_arcs);
    void prune(BPNode* node); 

    // --- Getters & Setters ---
    double get_best_upper_bound() const;
    double get_best_lower_bound() const;
    SearchStrategy get_current_strategy() const;
    std::vector<Column> get_incumbent_solution() const;
    BPNode* get_current_node() const;
    
    void set_best_upper_bound(double upper_bound);
    void set_best_lower_bound(double lower_bound);
    void set_incumbent_solution(double upper_bound, const std::vector<Column>& solution);

private:
    // Méthode interne pour l'initialisation de la racine
    void add_node_to_queues(BPNode* raw_node);

    // --- Gestion de la mémoire (Flat Ownership) ---
    std::vector<std::unique_ptr<BPNode>> all_generated_node_; 

    // --- Files d'attente (View pointers) ---
    std::stack<BPNode*> open_node_DFS_; 
    std::priority_queue<BPNode*, std::vector<BPNode*>, CompareNodeBound> open_node_BBF_;
    
    BPNode* current_node_;
    SearchStrategy current_strategy_;
    std::vector<Column> incumbent_solution_;
    double best_upper_bound_;
    double best_lower_bound_;
    static constexpr double INF = 1e9; 
};