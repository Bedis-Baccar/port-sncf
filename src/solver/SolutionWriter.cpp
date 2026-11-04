#include "SolutionWriter.h"
#include <fstream>
#include <iomanip>
#include <iostream>
#include "Trace.h"

void write_solution_json(const std::string& filename, const TimeSpaceGraph& graph,
                         const std::vector<Column>& best_sol, double upper_bound,
                         const SolveStatistics& st, bool link_conflicts) {
    std::ofstream out(filename);
    if (!out.is_open()) {
        std::cerr << "Erreur : Impossible de créer le fichier " << filename << std::endl;
        return;
    }
    out << std::setprecision(12);

    JsonObj g;
    g.integer("version", 2).integer("T", graph.get_horizon()).integer("t_s", graph.get_time_step())
     .integer("num_arcs", graph.get_arcs().size()).integer("num_nodes", graph.get_nodes().size())
     .boolean("link_conflicts", link_conflicts)
     .integer("conflict_resources", graph.get_nb_conflict_resources());
    JsonObj stats;
    stats.str("method", st.method).num("total_time_s", st.total_time_s).num("ttfs_s", st.time_to_first_sol_s)
         .num("root_lb", st.root_lb).num("final_lb", st.final_lb).num("final_ub", st.final_ub)
         .num("gap_pct", st.gap_pct).integer("explored_nodes", st.explored_nodes)
         .integer("pruned_nodes", st.pruned_nodes).integer("columns", st.num_columns)
         .num("time_master_s", st.time_in_master_s).num("time_pricing_s", st.time_in_pricing_s)
         .num("time_heuristics_s", st.time_in_heuristics_s);

    const bool none = best_sol.empty() || upper_bound >= 1e9;
    out << "{\n";
    out << "  \"instance\": \"" << JsonObj::escape(st.instance_name) << "\",\n";
    out << "  \"method\": \"" << st.method << "\",\n";
    out << "  \"status\": \"" << (none ? std::string("No integer solution found") : st.status) << "\",\n";
    out << "  \"graph\": " << g.dump() << ",\n";
    out << "  \"stats\": " << stats.dump() << ",\n";
    if (none) {
        out << "  \"train_paths\": []\n}\n";
        return;
    }
    out << "  \"objective_value\": " << upper_bound << ",\n";
    out << "  \"num_trains_routed\": " << best_sol.size() << ",\n";
    out << "  \"train_paths\": [\n";
    for (size_t i = 0; i < best_sol.size(); ++i) {
        const Column& col = best_sol[i];
        out << "    {\"train_id\": " << col.train_id << ", \"cost\": " << col.cost
            << ", \"arc_sequence\": " << JsonObj::int_array(col.arc_ids) << "}";
        if (i < best_sol.size() - 1) out << ",";
        out << "\n";
    }
    out << "  ]\n}\n";
    std::cout << "Meilleure solution sauvegardée dans : " << filename << std::endl;
}
