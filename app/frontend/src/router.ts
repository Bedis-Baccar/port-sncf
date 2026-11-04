import { useEffect, useState } from "react";

export type Route =
  | { page: "home" }
  | { page: "instance"; station: string; scenario: string }
  | { page: "run"; id: string; tab: string }
  | { page: "compare"; station?: string; scenario?: string };

export function parseHash(hash: string): Route {
  const parts = hash.replace(/^#\/?/, "").split("/").map(decodeURIComponent);
  if (parts[0] === "instance" && parts.length >= 3) return { page: "instance", station: parts[1], scenario: parts[2] };
  if (parts[0] === "run" && parts[1]) return { page: "run", id: parts[1], tab: parts[2] || "simulation" };
  if (parts[0] === "compare") return parts.length >= 3 ? { page: "compare", station: parts[1], scenario: parts[2] } : { page: "compare" };
  return { page: "home" };
}

export const href = {
  home: () => "#/",
  instance: (station: string, scenario: string) => `#/instance/${encodeURIComponent(station)}/${encodeURIComponent(scenario)}`,
  run: (id: string, tab = "simulation") => `#/run/${encodeURIComponent(id)}/${tab}`,
  compare: (station?: string, scenario?: string) =>
    station && scenario ? `#/compare/${encodeURIComponent(station)}/${encodeURIComponent(scenario)}` : "#/compare",
};

export function useRoute(): Route {
  const [route, setRoute] = useState(() => parseHash(location.hash));
  useEffect(() => {
    const on = () => setRoute(parseHash(location.hash));
    window.addEventListener("hashchange", on);
    return () => window.removeEventListener("hashchange", on);
  }, []);
  return route;
}

export const navigate = (h: string) => {
  location.hash = h;
};
