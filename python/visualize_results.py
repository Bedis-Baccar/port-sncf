import argparse
import csv
import os
import re

import matplotlib.colors as mcolors
import matplotlib.patches as mpatches
import matplotlib.pyplot as plt
import networkx as nx


def sanitize_label(s: str) -> str:
    out = []
    for c in s:
        out.append(c if c.isalnum() else "_")
    sanitized = "".join(out)
    return sanitized if sanitized else "unknown"


class ModelRunner:
    """
    Parses logfile/output.txt produced by tusp_solver and provides plotting helpers.
    """

    def __init__(self, log_file: str, output_file: str, instance_dir: str = "") -> None:
        self.log_file = log_file
        self.output_file = output_file
        self.instance_dir = instance_dir
        self.data = {}

    def _read_csv(self, path: str):
        rows = []
        with open(path, newline="") as f:
            reader = csv.reader(f)
            for row in reader:
                if not row:
                    continue
                rows.append(row)
        return rows

    def _load_instance_data(self) -> None:
        if not self.instance_dir:
            return
        if not os.path.isdir(self.instance_dir):
            print(
                f"Warning: Instance directory not found at {self.instance_dir}. "
                "Use --instance-dir to override."
            )
            return

        node_input_path = os.path.join(self.instance_dir, "Node input.csv")
        node_types_path = os.path.join(self.instance_dir, "Node types.csv")
        link_input_path = os.path.join(self.instance_dir, "Link input.csv")

        node_input = {}
        try:
            rows = self._read_csv(node_input_path)
            for row in rows[1:]:
                if len(row) < 3:
                    continue
                node_id = int(row[0].strip().lstrip("\ufeff"))
                node_input[node_id] = {
                    "name": row[1].strip(),
                    "type": int(row[2].strip()),
                    "links": [],
                }
        except FileNotFoundError:
            print(f"Warning: Node input file not found at {node_input_path}")
            pass
        except Exception as e:
            print(f"Warning: Error reading node input: {e}")

        link_data = {}
        try:
            rows = self._read_csv(link_input_path)
            for row in rows[1:]:
                if len(row) < 3:
                    continue
                origin = int(row[1].strip().lstrip("\ufeff"))
                destination = int(row[2].strip())
                link_data.setdefault(origin, []).append(destination)
        except FileNotFoundError:
            print(f"Warning: Link input file not found at {link_input_path}")
            pass
        except Exception as e:
            print(f"Warning: Error reading link input: {e}")

        type_input = {}
        try:
            rows = self._read_csv(node_types_path)
            for row in rows[1:]:
                if len(row) < 2:
                    continue
                type_id = int(row[0].strip().lstrip("\ufeff"))
                type_input[type_id] = {"type name": row[1].strip()}
        except FileNotFoundError:
            print(f"Warning: Node types file not found at {node_types_path}")
            pass
        except Exception as e:
            print(f"Warning: Error reading node types input: {e}")

        if node_input:
            nodes = self.data.get("Nodes")
            if not nodes:
                nodes = node_input
            for node_id, links in link_data.items():
                if node_id in nodes and not nodes[node_id].get("links"):
                    nodes[node_id]["links"] = links
            self.data["Nodes"] = nodes
        if type_input and not self.data.get("Node types"):
            self.data["Node types"] = type_input

    def _parse_output(self) -> None:
        try:
            with open(self.log_file, "r") as f:
                log_content = f.read()
            objective_value = []
            for objective_match in re.finditer(
                r"Obtainted objective value: (\S+)", log_content
            ):
                objective_value.append(objective_match.group(1))
            self.data["objective_value"] = objective_value
        except FileNotFoundError:
            print(f"Warning: Log file not found at {self.log_file}")
        except Exception as e:
            print(f"Warning: Error reading log file: {e}")

        try:
            with open(self.output_file, "r") as f:
                output_file = f.read()

            node_input = {}
            lines = output_file.splitlines()
            i = 0
            while i < len(lines):
                line = lines[i].strip()
                if line.startswith("Node ID:"):
                    try:
                        node_id = int(line.split(":", 1)[1].strip())
                    except ValueError:
                        i += 1
                        continue
                    node_name = ""
                    node_type = None
                    node_links = []

                    if i + 1 < len(lines) and lines[i + 1].startswith("Name:"):
                        node_name = lines[i + 1].split(":", 1)[1].strip()
                    if i + 2 < len(lines) and lines[i + 2].startswith("Type:"):
                        try:
                            node_type = int(lines[i + 2].split(":", 1)[1].strip())
                        except ValueError:
                            node_type = None
                    if i + 3 < len(lines) and lines[i + 3].startswith("Links:"):
                        links_str = lines[i + 3].split(":", 1)[1].strip()
                        if links_str:
                            node_links = [
                                int(l.strip())
                                for l in links_str.split(",")
                                if l.strip()
                            ]
                    if node_type is not None:
                        node_input[node_id] = {
                            "name": node_name,
                            "type": node_type,
                            "links": node_links,
                        }
                    i += 1
                    continue
                i += 1
            if node_input:
                self.data["Nodes"] = node_input

            type_input = {}
            for type_match in re.finditer(
                r"Type ID: (\d+)\nName: ([\w\s,-]*?)\n",
                output_file,
            ):
                type_input[int(type_match.group(1).strip())] = {
                    "type name": type_match.group(2).strip()
                }
            if type_input:
                self.data["Node types"] = type_input

            sections = re.split(r"-{3,}\s*(arc-path|node-arc)\s*-{3,}", output_file)
            parsed_paths = {}
            for i in range(1, len(sections), 2):
                section_type = sections[i]
                section_content = sections[i + 1]
                paths = {}
                for train_match in re.finditer(
                    r"Train ([\w\s,-:()]*?):\nVisited nodes: \n([\d\s,]*?)\nVisited time stamps: \n([\d\s,]*?)(?:\n|\Z)",
                    section_content,
                ):
                    train_name = train_match.group(1).strip()
                    node_ids = [
                        s.strip() for s in train_match.group(2).split(",") if s.strip()
                    ]
                    time_stamps = [
                        int(t.strip())
                        for t in train_match.group(3).split(",")
                        if t.strip()
                    ]
                    paths[train_name] = {
                        "node_ids": node_ids,
                        "time_stamps": time_stamps,
                    }
                parsed_paths[section_type] = paths
            self.data["train_paths"] = parsed_paths
        except FileNotFoundError:
            print(f"Warning: Output file not found at {self.output_file}")
        except Exception as e:
            print(f"Warning: Error reading output file: {e}")

        # Fallback to instance CSVs if output parsing is missing node/link/type data.
        self._load_instance_data()

    def get_data(self) -> dict:
        return self.data

    def plot_node_links(self) -> None:
        """
        Plots the station links to visualize the physical station and train movements.
        """
        S = nx.Graph()
        data = self.get_data()
        Nodes = data.get("Nodes")
        Types = data.get("Node types")
        node_types = {}

        if not (Nodes and Types):
            print("No node input information found")
            return

        for node_id, node_data in Nodes.items():
            S.add_node(node_id, name=node_data["name"], type=node_data["type"])
            node_types[node_id] = node_data["type"]

        for node_id, node_data in Nodes.items():
            for link in node_data["links"]:
                S.add_edge(node_id, link)

        unique_node_types = set(node_types.values())
        distinct_colors = list(mcolors.BASE_COLORS.values())
        color_map = {
            node_type_id: distinct_colors[i % len(distinct_colors)]
            for i, node_type_id in enumerate(unique_node_types)
        }
        node_colors = [color_map[node_types[node]] for node in S.nodes()]
        labels = nx.get_node_attributes(S, "name")

        options = {"node_size": 100, "width": 1}
        nx.draw_kamada_kawai(
            S,
            labels=labels,
            with_labels=True,
            node_color=node_colors,
            **options,
        )

        legend_handles = [
            mpatches.Patch(
                color=color_map[node_type_id],
                label=f"{Types[node_type_id]['type name']}",
            )
            for node_type_id in Types.keys()
        ]
        plt.legend(handles=legend_handles, title="Node types")

    def path_plotter(self, path_type: str = "node-arc", legend_max: int = 25) -> None:
        data = self.get_data()
        train_paths = data.get("train_paths")
        section = train_paths.get(path_type, {}) if train_paths else {}
        Nodes = data.get("Nodes")

        if not (train_paths and Nodes):
            print("No train path data found to plot.")
            return

        names = [" "]
        for node_id, node in Nodes.items():
            names.append(node["name"])

        def train_sort_key(name: str):
            match = re.search(r"\d+", name)
            if match:
                return (int(match.group()), name)
            return (10**9, name)

        train_ids = list(section.keys())
        train_ids.sort(key=train_sort_key)
        n_trains = max(len(train_ids), 1)

        # Use a limited, distinct palette and combine with line styles + markers
        base_colors = plt.get_cmap("tab20").colors
        line_styles = ["-", "--", ":", "-."]
        markers = ["o", "s", "^", "D", "v", "P", "X", "*"]
        style_count = len(base_colors) * len(line_styles) * len(markers)

        for idx, train_id in enumerate(train_ids):
            path_data = section[train_id]
            node_ids = [int(node_id) for node_id in path_data["node_ids"]]
            time_stamps = path_data["time_stamps"]
            if len(node_ids) != len(time_stamps):
                print(
                    f"Warning: Train {train_id} has mismatched number of nodes and time stamps. Skipping."
                )
                continue
            color = base_colors[idx % len(base_colors)]
            style_idx = (idx // len(base_colors)) % len(line_styles)
            marker_idx = (idx // (len(base_colors) * len(line_styles))) % len(markers)
            plt.plot(
                time_stamps,
                node_ids,
                marker=markers[marker_idx],
                linestyle=line_styles[style_idx],
                color=color,
                label=f"{train_id}",
            )

        plt.xlabel("Time")
        plt.ylabel("Physical node")
        plt.title(path_type + " train paths")
        if len(train_ids) <= legend_max:
            ncol = 1
            if len(train_ids) > 15:
                ncol = 2
            plt.legend(fontsize="small", ncol=ncol)
        else:
            print(
                f"Legend skipped: {len(train_ids)} trains (limit {legend_max})."
            )
        plt.yticks(range(len(names)), names)


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Visualize a solved instance using logfile/output from tusp_solver."
    )
    parser.add_argument("station", type=str, help="Station name (as used in tusp_solver)")
    parser.add_argument("scenario", type=str, help="Scenario number")
    parser.add_argument(
        "--viz-dir",
        type=str,
        default="",
        help="Override visualization directory (default: visualization/<station>_scenario_<scenario>)",
    )
    parser.add_argument(
        "--fig-dir",
        type=str,
        default="",
        help="Directory to save figures (default: visualizations/<station>_scenario_<scenario>)",
    )
    parser.add_argument(
        "--instance-dir",
        type=str,
        default="",
        help="Override instance directory (default: instances/<station>)",
    )
    parser.add_argument(
        "--log-file",
        type=str,
        default="",
        help="Override logfile path (default: <viz-dir>/logfile.log)",
    )
    parser.add_argument(
        "--output-file",
        type=str,
        default="",
        help="Override output path (default: <viz-dir>/output.txt)",
    )
    args = parser.parse_args()

    repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    safe_station = sanitize_label(args.station)
    safe_scenario = sanitize_label(args.scenario)
    viz_dir = args.viz_dir or os.path.join(
        repo_root, "solutions_log", f"{safe_station}_scenario_{safe_scenario}"
    )
    log_file = args.log_file or os.path.join(viz_dir, "logfile.log")
    output_file = args.output_file or os.path.join(viz_dir, "output.txt")

    fig_dir = args.fig_dir or os.path.join(
        repo_root, "visualizations", f"{safe_station}_scenario_{safe_scenario}"
    )
    os.makedirs(fig_dir, exist_ok=True)

    instance_dir = args.instance_dir or os.path.join(repo_root, "instances", args.station)
    runner = ModelRunner(
        log_file=log_file,
        output_file=output_file,
        instance_dir=instance_dir,
    )
    runner._parse_output()

    # Arc-path plots (column generation output).
    plt.figure(figsize=(16, 8))
    runner.path_plotter("arc-path")
    plt.savefig(os.path.join(fig_dir, "arc-path_train_paths.pdf"))
    plt.close()

    plt.figure(figsize=(16, 8))
    plt.subplot(121)
    runner.plot_node_links()
    plt.subplot(122)
    runner.path_plotter("arc-path")
    plt.savefig(os.path.join(fig_dir, "arc-path_network.pdf"))
    plt.close()

    # Station-only network plot.
    plt.figure(figsize=(8, 8))
    runner.plot_node_links()
    plt.savefig(os.path.join(fig_dir, "network.pdf"))
    plt.close()


if __name__ == "__main__":
    main()
