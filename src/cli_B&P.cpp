#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <cstdlib> // Pour std::stod

#include "GraphParser.h"
#include "GraphBuilder.h"
#include "TimeSpaceGraph.h"
#include "PricingProblem.h"
#include "MasterProblem.h"
#include "SearchTree.h"
#include "BranchAndPriceSolver.h"

namespace fs = std::filesystem;



void print_usage(const std::string& program_name) {
    std::cout << "\n=======================================================\n";
    std::cout << "SOLVEUR BRANCH-AND-PRICE\n";
    std::cout << "=======================================================\n";
    std::cout << "Utilisation : " << program_name << " -f <chemin_fichier> [-t <temps_limite>] [-n <nom du fichier>]\n\n";
    std::cout << "Options:\n";
    std::cout << "  -f, --file    Chemin vers le fichier de l'instance (Requis)\n";
    std::cout << "  -t, --time    Temps limite de resolution en secondes (Defaut: 3600)\n";
    std::cout << "  -n, --name     Nom personalisé de l'instance (Defaut : chemin vers le fichier)";
    std::cout << "  -h, --help    Affiche ce message d'aide\n";
    std::cout << "=======================================================\n\n";
}

int main(int argc, char** argv) {
    std::string file_path = "";
    std::string instance_name ="";
    double time_limit_s = 3600.0; // 1 heure par défaut
    bool flag_instance_name = false;
    // --- 1. PARSING DES ARGUMENTS CLI ---
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } 
        else if (arg == "-f" || arg == "--file") {
            if (i + 1 < argc) {
                file_path = argv[++i];
            } else {
                std::cerr << "Erreur : L'argument -f necessite un chemin de fichier.\n";
                return 1;
            }
        } 
        else if (arg == "-t" || arg == "--time") {
            if (i + 1 < argc) {
                time_limit_s = std::stod(argv[++i]); // Convertit le string en double
            } else {
                std::cerr << "Erreur : L'argument -t necessite une valeur numerique.\n";
                return 1;
            }
        }
        else if (arg == "-n" || arg == "--name"){
            flag_instance_name = true;
            if(i + 1 < argc){
                instance_name = argv[++i];
            }
            else {
                std::cerr << "Erreur : L'argument -n necessite un nom de fichier\n";
                return 1;
            }
        }
    }
    if(!flag_instance_name){
        instance_name = file_path;
    }
    // Vérification que le fichier a bien été fourni
    if (file_path.empty()) {
        std::cerr << "Erreur : Le chemin du fichier est requis.\n";
        print_usage(argv[0]);
        return 1;
    }
    if(file_path.back() != '/'){
        file_path = file_path + '/';
    }
    // --- 2. INITIALISATION ET RÉSOLUTION ---
    try {
        std::cout << "\nInitialisation de la resolution pour : " << instance_name << "\n";
        std::cout << " Temps limite fixe a : " << time_limit_s << " secondes\n\n";

        // Création des dossiers de résultats au cas où ils n'existent pas
        fs::create_directories("../results/dynamics");
        fs::create_directories("../results/solutions");

        // Pipeline classique
        InstanceParser parser(file_path);
        InstanceData data = parser.parse();
        GraphBuilder builder(data);
        TimeSpaceGraph graph = builder.build();
        BranchAndPriceSolver solver(graph, data.trains);

        // Lancement du solveur
        SolveStatistics stats = solver.solve(time_limit_s, instance_name);

        // Export de la solution
        std::string sol_filename = "../results/solutions/solution_" + instance_name + ".json";
        solver.export_best_solution(sol_filename);

    } catch (const std::exception& e) {
        std::cerr << "\n EXCEPTION CRITIQUE DETECTEE :\n";
        std::cerr << e.what() << "\n";
        return 1;
    }

    return 0;
}