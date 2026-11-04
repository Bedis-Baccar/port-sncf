#include "MasterProblem.h"
#include "gurobi_c++.h"
#include <memory>
#include "Column.h"
#include <stdexcept>

using namespace std;

MasterProblem::MasterProblem(const TimeSpaceGraph& graph, const vector<Train>& trains):status_(MasterStatus::UNSOLVED)
{
    env_ = make_unique<GRBEnv>();
    env_->set(GRB_StringParam_LogFile, "../gurobi_log.txt");
    env_->set(GRB_IntParam_LogToConsole, 0);
    model_ = make_unique<GRBModel>(*env_);
    num_trains_ = trains.size();
    num_pnodes_ = graph.get_nb_pnodes();
    num_services_ = graph.get_nb_services();
    max_time_ = graph.get_horizon();
    time_step_ = graph.get_time_step();
    flow_constrs_ = vector<GRBConstr>(num_trains_);
    service_constrs_ = vector<vector<GRBConstr>>(num_trains_, vector<GRBConstr>(num_services_));
    service_required_ = vector<vector<bool>>(num_trains_, vector<bool>(num_services_));
    conflict_constrs_ = vector<vector<GRBConstr>>(num_pnodes_, vector<GRBConstr>(max_time_/time_step_ + 1));
    build_initial_model(trains, graph);
}

void MasterProblem::build_initial_model(const std::vector<Train>& trains, const TimeSpaceGraph& graph){
    int k;        // working train id
    GRBLinExpr L;
    
    model_->set(GRB_IntAttr_ModelSense, GRB_MINIMIZE);

    //Contraintes de flots
    for(auto& train : trains){
        k = train.get_ID();
        flow_constrs_[k] = model_->addConstr(0.0 , GRB_GREATER_EQUAL, 1.0, "Flow train " + to_string(k));
    }

    //Construction des contraintes de services (une par train et une par service)
    for(auto& train: trains){    
        k = train.get_ID();
        service_required_[k] = train.get_services();
        for (int s = 0; s < num_services_ ; s++)
        {
            if (service_required_[k][s])
            {
                service_constrs_[k][s] = model_->addConstr(0.0, GRB_GREATER_EQUAL, 1.0, "Train " + to_string(k) + " service " + to_string(s));
            }
        }


    }


    // Construction des contraintes de conflits
    for (int p = 0; p < num_pnodes_; p++)
    {
        for (int t = 0; time_step_*t <= max_time_; t += 1)
        { 
            conflict_constrs_[p][t] = model_->addConstr(0.0, GRB_LESS_EQUAL, 1.0, "Incompatibility " + to_string(p) + " at " + to_string(t));
        }
    }

}

void MasterProblem::solve(){
    model_->optimize();
    status_ = MasterStatus::SOLVED;
}

bool MasterProblem::is_optimal() const{
    return model_->get(GRB_IntAttr_Status) == GRB_OPTIMAL;
}

bool MasterProblem::is_infeasible() const {
    return model_->get(GRB_IntAttr_Status) == GRB_INFEASIBLE;
}

double MasterProblem::get_objective_value() const {
    return model_->get(GRB_DoubleAttr_ObjVal);
}

std::vector<double> MasterProblem::get_flow_duals() const{
    std::vector<double> flow_duals(num_trains_);
    for(int k = 0 ; k < num_trains_; k++){
        flow_duals[k] = flow_constrs_[k].get(GRB_DoubleAttr_Pi);
    }
    return flow_duals;
}

std::vector<std::vector<double>> MasterProblem::get_service_duals() const{
    std::vector<std::vector<double>> service_duals(num_trains_, std::vector<double>(num_services_, 0.));
    for(int k  = 0; k < num_trains_; k++){
        for(int s = 0; s < num_services_; s++){
            
            if(service_required_[k][s]) {
                service_duals[k][s] = service_constrs_[k][s].get(GRB_DoubleAttr_Pi);
            }
        }
    }
    return service_duals;
}

std::vector<std::vector<double>> MasterProblem::get_conflict_duals() const{
    std::vector<std::vector<double>> conflict_duals(num_pnodes_, std::vector<double>(max_time_/time_step_ + 1));
    for(int p  = 0; p < num_pnodes_; p++){
        for(int t = 0; time_step_ * t <= max_time_; t+=1){
            conflict_duals[p][t] = conflict_constrs_[p][t].get(GRB_DoubleAttr_Pi);
        }
    }
    return conflict_duals;
}


void MasterProblem::convert_to_integer() {
    for (GRBVar& var : column_vars_) {
        var.set(GRB_CharAttr_VType, GRB_BINARY);
    }
    model_->update();
    status_ = MasterStatus::UNSOLVED;
}

void MasterProblem::convert_to_continuous() {
    for (GRBVar& var : column_vars_) {
        var.set(GRB_CharAttr_VType, GRB_CONTINUOUS);
    }
    model_->update();
    status_ = MasterStatus::UNSOLVED;
}

void MasterProblem::add_column(const Column& col, const TimeSpaceGraph& graph) {
    GRBColumn grb_col;
    
    //Ajout dans la contrainte de flot
    grb_col.addTerm(1.0, flow_constrs_[col.train_id]);

    // Ajout dans les contraintes de service
    for (int s_id = 0; s_id < num_services_; ++s_id) {
        if (col.services[s_id] && service_required_[col.train_id][s_id]) {
            grb_col.addTerm(1.0, service_constrs_[col.train_id][s_id]);
        }
    }

    // Ajout dans les contraintes de conflits (Incompatibilité)
    for (int arc_id : col.arc_ids) {
        const Arc& arc = graph.get_arc(arc_id);
        for (const auto& node_time : arc.get_iNodes()) {
            int p = node_time.first;
            int t = node_time.second;
            grb_col.addTerm(1.0, conflict_constrs_[p][t / time_step_]);
        }
    }


    GRBVar new_var = model_->addVar(
        0.0,             
        GRB_INFINITY,    
        col.cost,        
        GRB_CONTINUOUS, 
        grb_col,
        "Col_Train_" + std::to_string(col.train_id) + "_ID_" + std::to_string(col.ID)
    );

    column_vars_.push_back(new_var);
    status_ = MasterStatus::UNSOLVED;
}

MasterProblem::~MasterProblem() {
    model_.reset(); 
    env_.reset(); 
}

std::vector<int> MasterProblem::get_active_columns_ids() const {
    std::vector<int> active_ids;
    for (size_t i = 0; i < column_vars_.size(); ++i) {
        if (column_vars_[i].get(GRB_DoubleAttr_X) > 1e-5) {
            active_ids.push_back(static_cast<int>(i)); 
        }
    }
    
    return active_ids;
}

double MasterProblem::get_column_value(int id) const{
    return column_vars_[id].get(GRB_DoubleAttr_X);
}

void MasterProblem::enable_columns(const std::vector<int>& col_id){
    for(int id : col_id){
        column_vars_[id].set(GRB_DoubleAttr_UB, GRB_INFINITY);
    }
    status_ = MasterStatus::UNSOLVED;
}

void MasterProblem::disable_columns(const std::vector<int>& col_id){
    for(int id : col_id){
        column_vars_[id].set(GRB_DoubleAttr_UB, 0.0);
    }
    status_ = MasterStatus::UNSOLVED;
}

MasterStatus MasterProblem::get_master_status() const {
    return status_;
}

bool MasterProblem::is_sol_integer() const {
    if(status_ == MasterStatus::UNSOLVED){
        throw std::runtime_error("Trying to determine the nature of the solution of an unsolved model !");
    }
    const std::vector<int> active_id = get_active_columns_ids();
    for(int id : active_id){
        if(get_column_value(id) < 1 - 1e-5){
            return false;
        }
    }
    return true;
}

bool MasterProblem::is_sol_fractional() const {
    if(status_ == MasterStatus::UNSOLVED){
        throw std::runtime_error("Trying to determine the nature of the solution of an unsolved model !");
    }
    return !is_sol_integer();
}

int MasterProblem::get_gurobi_status() const {
    return model_->get(GRB_IntAttr_Status);
}

void MasterProblem::set_time_limit(double time_limit_seconds){
    model_->set(GRB_DoubleParam_TimeLimit, time_limit_seconds);
    return;
}

void MasterProblem::set_focus_to_finding_sol(){
    model_->set(GRB_IntParam_MIPFocus, 1);
}

void MasterProblem::set_focus_to_auto(){
    model_->set(GRB_IntParam_MIPFocus, 0);
}

int MasterProblem::get_current_nb_solutions() const{
    return model_->get(GRB_IntAttr_SolCount);
}