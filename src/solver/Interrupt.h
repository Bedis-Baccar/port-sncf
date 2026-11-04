#pragma once
#include <atomic>

// Asynchronous stop request shared by every solve method (set from a signal handler:
// Ctrl-C / SIGTERM / stop button of the web app). The solvers finish their current
// step, then stop and still export their best solution.
namespace tusp {
inline std::atomic<bool> g_stop_requested{false};
inline void request_stop() { g_stop_requested.store(true); }
inline bool stop_requested() { return g_stop_requested.load(); }
}
