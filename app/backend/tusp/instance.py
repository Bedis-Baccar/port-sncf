"""Instance loader — mirrors src/parser/GraphParser.cpp.

All IDs are converted to 0-based indices exactly like the C++ parser does,
so that train / node / service indices match the solver outputs.
"""
from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path


def _read_csv(path: Path) -> list[list[str]]:
    # utf-8-sig drops the BOM some files start with; C++ splits on ',' only.
    with open(path, encoding="utf-8-sig", newline="") as f:
        return [line.rstrip("\r\n").split(",") for line in f if line.strip()]


def _int(s: str) -> int:
    return int(s.strip())


@dataclass
class Service:
    id: int
    name: str
    cost: int
    duration: int


@dataclass
class NodeType:
    id: int
    name: str
    io: bool
    parking: bool
    dwelling: bool
    services: list[bool]
    nodes: list[int] = field(default_factory=list)


@dataclass
class PNode:
    id: int
    name: str
    type: int


@dataclass
class PLink:
    id: int
    origin: int
    destination: int
    length: int


@dataclass
class TrainType:
    id: int
    name: str
    w_shu: int
    t_turn: int


@dataclass
class Train:
    id: int
    name: str
    type: int
    a_t: int
    d_t: int
    services: list[bool]

    @property
    def required_services(self) -> list[int]:
        return [s for s, r in enumerate(self.services) if r]


@dataclass
class Instance:
    station: str
    scenario: str
    services: list[Service]
    node_types: list[NodeType]
    pnodes: list[PNode]
    plinks: list[PLink]
    train_types: list[TrainType]
    trains: list[Train]

    def to_dict(self) -> dict:
        return {
            "station": self.station,
            "scenario": self.scenario,
            "services": [vars(s) for s in self.services],
            "node_types": [vars(t) for t in self.node_types],
            "nodes": [vars(p) for p in self.pnodes],
            "links": [vars(l) for l in self.plinks],
            "train_types": [vars(t) for t in self.train_types],
            "trains": [
                {**vars(t), "required_services": t.required_services} for t in self.trains
            ],
        }


def load_instance(scenario_dir: Path) -> Instance:
    scenario_dir = Path(scenario_dir)
    station_dir = scenario_dir.parent

    services = [
        Service(_int(r[0]) - 1, r[1].strip(), _int(r[2]), _int(r[3]))
        for r in _read_csv(scenario_dir / "Service input.csv")[1:]
    ]
    ns = len(services)

    node_types = []
    for r in _read_csv(station_dir / "Node types.csv")[1:]:
        node_types.append(
            NodeType(
                _int(r[0]) - 1,
                r[1].strip(),
                r[2].strip() == "1",
                r[3].strip() == "1",
                r[4].strip() == "1",
                [_int(r[5 + j]) == 1 for j in range(ns)],
            )
        )

    pnodes = []
    for r in _read_csv(station_dir / "Node input.csv")[1:]:
        p = PNode(_int(r[0]) - 1, r[1].strip(), _int(r[2]) - 1)
        pnodes.append(p)
        node_types[p.type].nodes.append(p.id)

    plinks = [
        PLink(_int(r[0]) - 1, _int(r[1]) - 1, _int(r[2]) - 1, _int(r[3]))
        for r in _read_csv(station_dir / "Link input.csv")[1:]
    ]

    train_types = [
        TrainType(_int(r[0]) - 1, r[1].strip(), _int(r[2]), _int(r[3]))
        for r in _read_csv(scenario_dir / "Train types.csv")[1:]
    ]

    trains = [
        Train(_int(r[0]) - 1, r[1].strip(), _int(r[2]) - 1, _int(r[3]), _int(r[4]), [False] * ns)
        for r in _read_csv(scenario_dir / "Train input.csv")[1:]
    ]

    for r in _read_csv(scenario_dir / "Operations assignment.csv")[1:]:
        trains[_int(r[2]) - 1].services[_int(r[1]) - 1] = True

    return Instance(
        station=station_dir.name,
        scenario=scenario_dir.name,
        services=services,
        node_types=node_types,
        pnodes=pnodes,
        plinks=plinks,
        train_types=train_types,
        trains=trains,
    )
