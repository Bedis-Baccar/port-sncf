#pragma once
#include <string>
#include <vector>
#include "Column.h"
#include "SolveStatistics.h"
#include "TimeSpaceGraph.h"

// Writes solution.json (same format for every solve method, read by the web app).
void write_solution_json(const std::string& filename, const TimeSpaceGraph& graph,
                         const std::vector<Column>& solution, double upper_bound,
                         const SolveStatistics& stats, bool link_conflicts);
