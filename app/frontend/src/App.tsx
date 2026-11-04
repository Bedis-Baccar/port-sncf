import { useEffect, useState } from "react";
import { api, type Health } from "./api";
import ComparePage from "./pages/ComparePage";
import Home from "./pages/Home";
import InstancePage from "./pages/InstancePage";
import RunPage from "./pages/RunPage";
import { href, useRoute } from "./router";

export default function App() {
  const route = useRoute();
  const [health, setHealth] = useState<Health | null>(null);

  useEffect(() => {
    const load = () => api.health().then(setHealth).catch(() => setHealth(null));
    load();
    const t = setInterval(load, 10000);
    return () => clearInterval(t);
  }, []);

  return (
    <>
      <header className="topbar">
        <a className="brand" href={href.home()}><span className="logo">🚆</span>TUSP Visualizer</a>
        <nav>
          <a href={href.home()} className={route.page === "home" || route.page === "instance" ? "active" : ""}>Instances</a>
          <a href={href.compare()} className={route.page === "compare" ? "active" : ""}>Benchmark</a>
        </nav>
        <div className="spacer" />
        {health?.active_runs.map((id) => (
          <a key={id} href={href.run(id, "convergence")} className="chip live info">{id}</a>
        ))}
        {health && (
          <span className={`chip ${health.solver_available ? "ok" : "warn"}`} title={health.solver_path ?? "cli_tusp_solver not found"}>
            {health.solver_available ? "solver ready" : "replay only"}
          </span>
        )}
      </header>
      <main>
        {route.page === "home" && <Home health={health} />}
        {route.page === "instance" && <InstancePage station={route.station} scenario={route.scenario} health={health} />}
        {route.page === "run" && <RunPage key={route.id} id={route.id} tab={route.tab} health={health} />}
        {route.page === "compare" && <ComparePage health={health} station={route.station} scenario={route.scenario} />}
      </main>
    </>
  );
}
