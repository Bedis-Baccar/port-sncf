#include "SearchTree.h"
#include <stdexcept>
#include <algorithm>
#include <map>
#include <vector>
#include <stack>

// Constructeur par défaut (Arbre vide)
SearchTree::SearchTree():
    best_upper_bound_(INF),
    best_lower_bound_(-INF),
    current_strategy_(SearchStrategy::BBF),
    current_node_(nullptr)
{

    auto root = std::make_unique<BPNode>(BPNode{
        nullptr,                    // parent
        std::vector<BPNode*>(),     // children
        0,                          // depth
        -INF,                       // lower_bound
        NodeStatus::UNPROCESSED,    // status
        std::vector<int>(),                         // train_ids
        std::map<int, std::vector<int>>()          // forbidden_arcs_ids
    });
    
    current_node_ = root.get();
    add_node_to_queues(root.get());
    all_generated_node_.push_back(std::move(root)); // L'arbre prend possession de la racine
}

// Constructeur avec solution initiale (Warm start)
SearchTree::SearchTree(const std::vector<Column>& incumbent_solution, double value):
    incumbent_solution_(incumbent_solution),
    best_upper_bound_(value),
    best_lower_bound_(-INF),
    current_strategy_(SearchStrategy::DFS),
    current_node_(nullptr)
{
    std::unique_ptr<BPNode> root = std::make_unique<BPNode>(BPNode{
        nullptr, std::vector<BPNode*>(), 0, -INF, NodeStatus::UNPROCESSED, std::vector<int>(), std::map<int, std::vector<int>>()
    });
    
    current_node_ = root.get();
    add_node_to_queues(root.get());
    all_generated_node_.push_back(std::move(root));
}

// --- Navigation et Stratégie ---

void SearchTree::set_strategy_DFS() {
    current_strategy_ = SearchStrategy::DFS;
    open_node_DFS_ = std::stack<BPNode*>(); 
}

void SearchTree::set_strategy_BBF() {
    current_strategy_ = SearchStrategy::BBF;
    
    // Transfert de sauvetage : on récupère les branches inexplorées du plongeon DFS
    while (!open_node_DFS_.empty()) {
        BPNode* node = open_node_DFS_.top();
        open_node_DFS_.pop();
        
        // On ne transfère que les nœuds qui n'ont pas encore été traités
        if (node->status == NodeStatus::UNPROCESSED) {
            open_node_BBF_.push(node);
        }
    }
}

BPNode* SearchTree::get_next_node() {
    if (current_strategy_ == SearchStrategy::DFS) {
        while (!open_node_DFS_.empty()) {
            BPNode* candidate = open_node_DFS_.top();
            open_node_DFS_.pop();
            
            if (candidate->status == NodeStatus::UNPROCESSED) {
                return candidate;
            }
        }
        set_strategy_BBF(); 
        return get_next_node();
        
    } else {
        while (!open_node_BBF_.empty()) {
            BPNode* candidate = open_node_BBF_.top();
            open_node_BBF_.pop();
            
            if (candidate->status == NodeStatus::UNPROCESSED) {
                return candidate;
            }
        }
    }
    
    current_node_ = nullptr;
    return nullptr; // L'arbre entier a été exploré
}

// --- Construction de l'arbre ---

BPNode* SearchTree::create_child_node(BPNode* parent, const std::vector<int>& train_ids, const std::map <int, std::vector<int>>& additional_forbidden_arcs) {
    if (parent == nullptr) throw std::runtime_error("Cannot create a child without a parent.");


    // 2. Création du nœud
    auto new_node = std::make_unique<BPNode>(
        parent,                     
        std::vector<BPNode*>(),     
        parent->depth + 1,          
        parent->lower_bound,        
        NodeStatus::UNPROCESSED,    
        train_ids,                   
        additional_forbidden_arcs               
    );


    BPNode* raw_ptr = new_node.get();
    parent->children.push_back(raw_ptr);


    add_node_to_queues(raw_ptr);
    all_generated_node_.push_back(std::move(new_node)); 

    return raw_ptr;
}

void SearchTree::add_node_to_queues(BPNode* raw_node) {
    if (current_strategy_ == SearchStrategy::DFS) {
        open_node_DFS_.push(raw_node);
    } else {
        open_node_BBF_.push(raw_node);
    }
}


void SearchTree::prune(BPNode* node) {
    if (node == nullptr) return;

    node->status = NodeStatus::PRUNED;

    node->forbidden_arcs_ids.clear(); 
    node->train_ids.clear();
    node->train_ids.shrink_to_fit();

}

// --- Getters & Setters ---

double SearchTree::get_best_upper_bound() const { return best_upper_bound_; }
double SearchTree::get_best_lower_bound() const {
    double bound = INF; 

    if (!open_node_BBF_.empty()) {
        bound = open_node_BBF_.top()->lower_bound;
    }
    
    if (current_node_ != nullptr && (current_node_->status == NodeStatus::PROCESSING || current_node_->status == NodeStatus::UNPROCESSED)) {
        if (current_node_->lower_bound < bound) {
            bound = current_node_->lower_bound;
        }
    }
    
    if (bound == INF) {
        return best_upper_bound_;
    }
    
    return bound;
}
SearchStrategy SearchTree::get_current_strategy() const { return current_strategy_; }
std::vector<Column> SearchTree::get_incumbent_solution() const { return incumbent_solution_; }

void SearchTree::set_best_upper_bound(double upper_bound) {
    if (upper_bound > best_upper_bound_) {
        throw std::runtime_error("Trying to set an upper bound that is worse than the current one.");
    }
    best_upper_bound_ = upper_bound;
}


void SearchTree::set_incumbent_solution(double upper_bound, const std::vector<Column>& solution) {
    set_best_upper_bound(upper_bound);
    incumbent_solution_ = solution;
}


Trajectory SearchTree::trajectory_to_node(BPNode* target_node) {
    Trajectory traj;
    if (current_node_ == target_node) return traj;

    BPNode* curr_up = current_node_;
    BPNode* curr_down = target_node;

    // 1. Égaliser les profondeurs
    while (curr_up && curr_down && curr_up->depth > curr_down->depth) {
        traj.nodes_to_revert.push_back(curr_up);
        curr_up = curr_up->parent;
    }
    while (curr_up && curr_down && curr_down->depth > curr_up->depth) {
        traj.nodes_to_apply.push_back(curr_down); // On stocke pour le chemin descendant
        curr_down = curr_down->parent;
    }

    // 2. Remonter jusqu'au LCA
    while (curr_up != curr_down) {
        if (curr_up) {
            traj.nodes_to_revert.push_back(curr_up);
            curr_up = curr_up->parent;
        }
        if (curr_down) {
            traj.nodes_to_apply.push_back(curr_down);
            curr_down = curr_down->parent;
        }
    }

    // 3. Inverser le chemin descendant (car on l'a construit de bas en haut)
    std::reverse(traj.nodes_to_apply.begin(), traj.nodes_to_apply.end());

    current_node_ = target_node;
    return traj;
}


BPNode* SearchTree::get_current_node() const {
    return current_node_;
}