#include <iostream>
#include <string>
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

    double time_limit_s = 3600.0;

    fs::create_directories("../results/dynamics");
    fs::create_directories("../results/solutions");

    

    // Préparation du fichier de synthèse global
    std::ofstream summary_file("../results/summary_benchmark.csv");
    summary_file << "Instance,Trains,Arcs,Root_LB,Final_LB,Final_UB,Gap_%,TotalTime_s,TTFS_s,Nodes,MasterTime_s,PricingTime_s,HeuristicsTime_s\n";
    int i = 0;
    // Boucle de résolution
    for (const auto& path : instance_paths) {
        std::string name = instance_name[i];
        std::cout << "\n=======================================================\n";
        std::cout << " LANCEMENT INSTANCE : " << name << "\n";
        std::cout << "=======================================================\n";

        InstanceParser parser(path);
        InstanceData data = parser.parse();
        GraphBuilder builder(data);
        TimeSpaceGraph graph = builder.build();
        BranchAndPriceSolver solver(graph, data.trains);

        SolveStatistics stats = solver.solve(time_limit_s, name);
        std::string sol_filename = "../results/solutions/solution_" + name + ".json";
        solver.export_best_solution(sol_filename);

        // Écriture dans le CSV global
        summary_file << stats.instance_name << ","
                     << stats.num_trains << ","
                     << stats.num_arcs << ","
                     << stats.root_lb << ","
                     << (stats.final_lb > - 1e9 ? std::to_string(stats.final_ub) : "") << ","
                     << (stats.final_ub < 1e9 ? std::to_string(stats.final_ub) : "") << ","
                     << std::max(0.0, stats.gap_pct) << ","
                     << stats.total_time_s << ","
                     << stats.time_to_first_sol_s << ","
                     << stats.explored_nodes << ","
                     << stats.time_in_master_s << ","
                     << stats.time_in_pricing_s << ","
                     << stats.time_in_heuristics_s << "\n";
                     
        summary_file.flush();
        ++i;
    }

    std::cout << "BENCHMARK TERMINÉ ! Résultats sauvegardés dans summary_benchmark.csv\n";
    return 0;
}