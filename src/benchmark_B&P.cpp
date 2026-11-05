#include <iostream>
#include <string>
#include <cmath>
#include <vector>
#include <queue>
#include <fstream>
#include <filesystem>

#include "GraphParser.h"
#include "GraphBuilder.h"
#include "TimeSpaceGraph.h"
#include "PricingProblem.h"
#include "MasterProblem.h"
#include "SearchTree.h"
#include "BranchAndPriceSolver.h"
#include "SolverParams.h"
#include "LnsSolver.h"

namespace fs = std::filesystem;

int main() {
    // Liste instances à remplir
    std::vector<std::string> instance_paths = {

        "../instances/large station/scenario 1/",
        "../instances/large station/scenario 2/",
        "../instances/large station/scenario 3/",
        "../instances/large station/scenario 4/",

        "../instances/variant station/scenario 1/"

        
    };
    std::vector<std::string> instance_name = {

        "large_station_sce_1",
        "large_station_sce_2",
        "large_station_sce_3",
        "large_station_sce_4",

        "variant_station_sce_1"
        
    };

    // Every convergence knob can be tuned here (see SolverParams.h)
    SolverParams base_params;
    base_params.time_limit_s = 3600.0;
    base_params.method = "bp";          // "bp" or "lns" (see SolverParams.h for the LNS settings)
    base_params.link_conflicts = true;  // false = original model (head-on crossings allowed)

    fs::create_directories("../results/runs");

    // Préparation du fichier de synthèse global
    std::ofstream summary_file("../results/summary_benchmark.csv");
    summary_file << "Instance,Trains,Arcs,Root_LB,Final_LB,Final_UB,Gap_%,TotalTime_s,TTFS_s,Nodes,MasterTime_s,PricingTime_s,HeuristicsTime_s,Method,LinkConflicts\n";
    for (size_t i = 0; i < instance_paths.size(); ++i) {
        const std::string& path = instance_paths[i];
        const std::string& name = instance_name[i];
        std::cout << "\n=======================================================\n";
        std::cout << " LANCEMENT INSTANCE : " << name << "\n";
        std::cout << "=======================================================\n";

        SolverParams params = base_params;
        params.out_dir = "../results/runs/" + name;
        fs::create_directories(params.out_dir);

        InstanceParser parser(path);
        InstanceData data = parser.parse();
        GraphBuilder builder(data, params.link_conflicts);
        TimeSpaceGraph graph = builder.build();
        SolveStatistics stats;
        if (params.method == "lns") {
            LnsSolver solver(graph, data.trains, params);
            stats = solver.solve(name);
            solver.export_best_solution(params.out_dir + "/solution.json");
        } else {
            BranchAndPriceSolver solver(graph, data.trains, params);
            stats = solver.solve(name);
            solver.export_best_solution(params.out_dir + "/solution.json");
        }

        summary_file << stats.instance_name << ","
                     << stats.num_trains << ","
                     << stats.num_arcs << ","
                     << stats.root_lb << ","
                     << (stats.final_lb > -1e9 ? std::to_string(stats.final_lb) : "") << ","
                     << (stats.final_ub < 1e9 ? std::to_string(stats.final_ub) : "") << ","
                     << (std::isnan(stats.gap_pct) ? std::string() : std::to_string(stats.gap_pct)) << ","
                     << stats.total_time_s << ","
                     << stats.time_to_first_sol_s << ","
                     << stats.explored_nodes << ","
                     << stats.time_in_master_s << ","
                     << stats.time_in_pricing_s << ","
                     << stats.time_in_heuristics_s << ","
                     << stats.method << ","
                     << (params.link_conflicts ? 1 : 0) << "\n";
        summary_file.flush();
    }

    std::cout << "BENCHMARK TERMINÉ ! Résultats sauvegardés dans summary_benchmark.csv\n";
    return 0;
}