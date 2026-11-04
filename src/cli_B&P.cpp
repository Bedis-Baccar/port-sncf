#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "GraphParser.h"
#include "GraphBuilder.h"
#include "TimeSpaceGraph.h"
#include "BranchAndPriceSolver.h"
#include "LnsSolver.h"
#include "SolverParams.h"

namespace fs = std::filesystem;

void print_usage(const std::string& program_name) {
    std::cout << "\n=======================================================\n";
    std::cout << "SOLVEUR BRANCH-AND-PRICE\n";
    std::cout << "=======================================================\n";
    std::cout << "Utilisation : " << program_name << " -f <dossier_scenario> [options]\n\n";
    std::cout << "Options generales :\n";
    std::cout << "  -f, --file <dir>          Dossier du scenario (Requis)\n";
    std::cout << "  -n, --name <name>         Nom de l'instance (Defaut : chemin du dossier)\n";
    std::cout << "  -t, --time <s>            Temps limite en secondes (Defaut : 3600)\n";
    std::cout << "  -o, --out <dir>           Dossier de sortie (Defaut : ../results/runs/<name>)\n";
    std::cout << "      --no-trace            Ne pas ecrire trace.jsonl\n";
    std::cout << "\nModele :\n";
    std::cout << "      --no-link-conflicts   Modele d'origine : autorise deux trains a se croiser\n";
    std::cout << "                            en sens inverse sur un meme lien (Defaut : interdit)\n";
    std::cout << "\nMethode :\n";
    std::cout << "      --method bp|lns       Branch-and-Price exact (Defaut) ou Large Neighbourhood Search\n";
    std::cout << "\nParametres LNS (--method lns) :\n";
    std::cout << "      --lns-frac f          Part initiale des trains liberes par voisinage (Defaut : 0.3)\n";
    std::cout << "      --lns-max-frac f      Part maximale avant redemarrage (Defaut : 0.7)\n";
    std::cout << "      --lns-mip-time s      Temps du MIP de finition, 0 = sans (Defaut : 5)\n";
    std::cout << "      --lns-cg-gap g        Gap de CG dans un voisinage (Defaut : 0.001)\n";
    std::cout << "      --lns-root-mip-time s Temps du MIP sur les colonnes racine (Defaut : 10)\n";
    std::cout << "      --lns-root-frac f     Part du temps pour la CG racine (Defaut : 0.3)\n";
    std::cout << "      --lns-ops a,b,...     Operateurs : random,time,related,dual,rejected (Defaut : tous)\n";
    std::cout << "      --lns-no-prune        N'utilise pas la borne pour elaguer les voisinages\n";
    std::cout << "      --lns-max-iter n      Nombre max d'iterations, 0 = jusqu'au temps limite\n";
    std::cout << "      --seed n              Graine aleatoire (Defaut : 1)\n";
    std::cout << "\nParametres de convergence (B&P ; --cg-gaps a,... donne aussi le gap racine du LNS) :\n";
    std::cout << "      --cg-gaps a,b,c,d     Gaps d'arret de la generation de colonnes\n";
    std::cout << "                            racine, prof.<=5, prof.<=10, au-dela (Defaut : 0,0.001,0.005,0.01)\n";
    std::cout << "      --heur-every <n>      Heuristiques primales tous les n noeuds, 0 = jamais (Defaut : 5)\n";
    std::cout << "      --no-diving           Desactive l'heuristique de plongee\n";
    std::cout << "      --dive-gap <g>        Gap de CG pendant la plongee (Defaut : 0.05)\n";
    std::cout << "      --no-mip              Desactive l'heuristique MIP sur le maitre restreint\n";
    std::cout << "      --mip-time <s>        Temps limite du MIP heuristique (Defaut : 15)\n";
    std::cout << "      --branching <rule>    auto (service_split puis quantum) | quantum (Defaut : auto)\n";
    std::cout << "      --dfs                 Exploration en profondeur d'abord (Defaut : meilleur bound)\n";
    std::cout << "      --threads <n>         Threads Gurobi, 0 = defaut Gurobi (Defaut : 0)\n";
    std::cout << "  -h, --help                Affiche ce message\n";
    std::cout << "=======================================================\n\n";
}

static void on_signal(int) {
    tusp::request_stop();
}

static std::vector<double> parse_list(const std::string& s) {
    std::vector<double> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ',')) {
        out.push_back(std::stod(item));
    }
    return out;
}

int main(int argc, char** argv) {
    std::string file_path;
    std::string instance_name;
    SolverParams params;

    auto need_value = [&](int& i, const std::string& arg) -> std::string {
        if (i + 1 >= argc) {
            throw std::invalid_argument("L'argument " + arg + " necessite une valeur.");
        }
        return argv[++i];
    };

    try {
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "-h" || arg == "--help") { print_usage(argv[0]); return 0; }
            else if (arg == "-f" || arg == "--file") file_path = need_value(i, arg);
            else if (arg == "-n" || arg == "--name") instance_name = need_value(i, arg);
            else if (arg == "-t" || arg == "--time") params.time_limit_s = std::stod(need_value(i, arg));
            else if (arg == "-o" || arg == "--out") params.out_dir = need_value(i, arg);
            else if (arg == "--no-trace") params.trace = false;
            else if (arg == "--no-link-conflicts") params.link_conflicts = false;
            else if (arg == "--cg-gaps") {
                params.cg_gaps = parse_list(need_value(i, arg));
                if (params.cg_gaps.size() != 4) throw std::invalid_argument("--cg-gaps attend 4 valeurs");
            }
            else if (arg == "--heur-every") params.heuristic_every = std::stoi(need_value(i, arg));
            else if (arg == "--no-diving") params.diving = false;
            else if (arg == "--dive-gap") params.dive_cg_gap = std::stod(need_value(i, arg));
            else if (arg == "--no-mip") params.mip_heuristic = false;
            else if (arg == "--mip-time") params.mip_time_s = std::stod(need_value(i, arg));
            else if (arg == "--branching") {
                std::string rule = need_value(i, arg);
                if (rule == "auto") params.branching = BranchingRule::AUTO;
                else if (rule == "quantum") params.branching = BranchingRule::QUANTUM;
                else throw std::invalid_argument("--branching : auto | quantum");
            }
            else if (arg == "--dfs") params.start_dfs = true;
            else if (arg == "--threads") params.threads = std::stoi(need_value(i, arg));
            else if (arg == "--method") {
                params.method = need_value(i, arg);
                if (params.method != "bp" && params.method != "lns") throw std::invalid_argument("--method : bp | lns");
            }
            else if (arg == "--lns-frac") params.lns_init_frac = std::stod(need_value(i, arg));
            else if (arg == "--lns-max-frac") params.lns_max_frac = std::stod(need_value(i, arg));
            else if (arg == "--lns-mip-time") params.lns_mip_time_s = std::stod(need_value(i, arg));
            else if (arg == "--lns-cg-gap") params.lns_cg_gap = std::stod(need_value(i, arg));
            else if (arg == "--lns-root-mip-time") params.lns_root_mip_time_s = std::stod(need_value(i, arg));
            else if (arg == "--lns-root-frac") params.lns_root_time_frac = std::stod(need_value(i, arg));
            else if (arg == "--lns-no-prune") params.lns_bound_pruning = false;
            else if (arg == "--lns-max-iter") params.lns_max_iter = std::stoi(need_value(i, arg));
            else if (arg == "--seed") params.lns_seed = std::stoi(need_value(i, arg));
            else if (arg == "--lns-ops") {
                params.lns_operators.clear();
                std::stringstream ss(need_value(i, arg));
                std::string op;
                while (std::getline(ss, op, ',')) {
                    if (op != "random" && op != "time" && op != "related" && op != "dual" && op != "rejected")
                        throw std::invalid_argument("operateur LNS inconnu : " + op);
                    params.lns_operators.push_back(op);
                }
            }
            else throw std::invalid_argument("Argument inconnu : " + arg);
        }
    } catch (const std::exception& e) {
        std::cerr << "Erreur : " << e.what() << "\n";
        print_usage(argv[0]);
        return 1;
    }

    if (file_path.empty()) {
        std::cerr << "Erreur : Le chemin du fichier est requis.\n";
        print_usage(argv[0]);
        return 1;
    }
    if (file_path.back() != '/') {
        file_path += '/';
    }
    if (instance_name.empty()) {
        instance_name = file_path;
    }
    if (params.out_dir.empty()) {
        params.out_dir = "../results/runs/" + instance_name;
    }

    // Ctrl-C / SIGTERM (stop button of the web app): stop cleanly and export the incumbent
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    try {
        std::cout << "\nInitialisation de la resolution pour : " << instance_name << "\n";
        std::cout << " Temps limite fixe a : " << params.time_limit_s << " secondes\n";
        std::cout << " Sorties : " << params.out_dir << "\n\n";
        fs::create_directories(params.out_dir);

        InstanceParser parser(file_path);
        InstanceData data = parser.parse();
        GraphBuilder builder(data, params.link_conflicts);
        TimeSpaceGraph graph = builder.build();
        if (params.method == "lns") {
            LnsSolver solver(graph, data.trains, params);
            solver.solve(instance_name);
            solver.export_best_solution(params.out_dir + "/solution.json");
        } else {
            BranchAndPriceSolver solver(graph, data.trains, params);
            solver.solve(instance_name);
            solver.export_best_solution(params.out_dir + "/solution.json");
        }

    } catch (const std::exception& e) {
        std::cerr << "\n EXCEPTION CRITIQUE DETECTEE :\n";
        std::cerr << e.what() << "\n";
        return 1;
    }

    return 0;
}
