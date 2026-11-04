# Instances Data Format

This folder organizes input data by physical station (or shunting yard) and by operational scenario.

## Directory layout

```text
instances/
  <station name>/
    Link input.csv
    Node input.csv
    Node types.csv
    scenario 1/
      Service input.csv
      Train types.csv
      Train input.csv
      Operations assignment.csv
    scenario 2/
      ...
```

- `<station name>` defines the physical infrastructure.
- `scenario N` defines an operational scenario on the same infrastructure.

## What is station-level vs scenario-level

Station-level files (shared by all scenarios in the same station):
- `Node types.csv`
- `Node input.csv`
- `Link input.csv`

Scenario-level files (can change between scenarios):
- `Service input.csv`
- `Train types.csv`
- `Train input.csv`
- `Operations assignment.csv`

## CSV schema by file

### Station-level files

`Node types.csv`
- Columns: `ID,name,io,parking,dwelling,service1,service2,...,serviceK`
- Meaning:
  - `ID`: node type id
  - `name`: node type label
  - `io`, `parking`, `dwelling`: binary flags (0/1)
  - `service*`: binary compatibility of this node type with each service id

`Node input.csv`
- Columns: `Node_id,Node_Name,Node_Type`
- Meaning:
  - `Node_id`: physical node id
  - `Node_Name`: node label
  - `Node_Type`: reference to `Node types.csv` `ID`

`Link input.csv`
- Columns: `link_id,from_node_id,to_node_id,length`
- Meaning:
  - `link_id`: physical link id
  - `from_node_id`, `to_node_id`: directed edge endpoints (node ids)
  - `length`: link length/cost unit used by the model

### Scenario-level files

`Service input.csv`
- Columns: `ID,Name,Cost,Duration`
- Meaning:
  - service id, label, fixed cost, processing duration
  - Service arc cost is fixed: `Cost` is used directly (it is not multiplied by duration).

`Train types.csv`
- Columns: `id,name,w_s,turning_time`
- Meaning:
  - train type id, label, shunting weight, turning time
  - `w_s` is a shunting weight (cost per unit time). The shunting arc cost is `w_s * duration + c_shu`, where `duration` is the link `length` and `c_shu` is a fixed constant in code.
  - Storage arc cost is `w_dwell * turning_time + c_park`, where `w_dwell` and `c_park` are fixed constants in code.

Fixed constants such as `t_h`, `c_shu`, `w_dwell`, and `c_park` are defined in `src/header/params.h`.
Current values (from code):
- `t_h = 5`
- `c_shu = 2`
- `w_dwell = 1`
- `c_park = 5`
`t_h` is the safety headway (time buffer) between two consecutive trains used to mark arcs as incompatible over time.

## Cost model (from code)

Arc cost formula: `cost = w * duration + c` (see `src/Arc.cpp`).

Shunting arcs: `duration = link length`, `w = w_s` (from `Train types.csv`), `c = c_shu` (fixed add-on per shunting arc).

Storage/Parking arcs: `duration = turning_time`, `w = w_dwell` (per time unit), `c = c_park` (fixed add-on per storage arc).

Dwelling arcs: `duration = t_s`, `w = w_dwell` (per time unit), `c = 0`.

Service arcs: `w = 0`, `c = service Cost` (fixed, not multiplied by duration).

`Train input.csv`
- Columns: `ID,Name,type,a_t,d_t`
- Meaning:
  - train id, label, train type id, arrival time, departure time

`Operations assignment.csv`
- Columns: `id,type,train ID`
- Meaning:
  - operation id, service id (`type`), target train id

## Time step (`t_s`) logic

The model discretizes time with a step `t_s` computed in code (not a fixed constant).
It is the greatest common divisor (GCD) of:
- all train arrival times `a_t`
- all train departure times `d_t`
- all service durations
- all train turning times
- all link lengths

This means all time-related inputs should be multiples of the same base unit; otherwise `t_s` will become small.
In the provided instances, all values are multiples of 5, so `t_s = 5`.

## Instance generation rule for train stay time

When generating synthetic scenarios, we use the following rule of thumb for each train `k`:
- `stay_k = d_t - a_t >= 2 * sum(service durations for k) + (t_s / 5) * nb_nodes`

This is meant to give enough (but not excessive) time to perform all assigned services, while scaling the slack with
station size. The stay time is rounded up to the nearest multiple of `t_s` in the generated files.

Generated instances are not guaranteed to allow all trains to be accepted. The goal is to host as many trains as
possible and perform as many services as possible under the constraints.

## Data consistency rules

The current input loader expects the following:
- IDs are contiguous and start at 1 in each file.
- Rows are ordered by their ID.
- Foreign keys are valid:
  - `Node_Type` references an existing node type.
  - `from_node_id` and `to_node_id` reference existing nodes.
  - train `type` references an existing train type.
  - operation `type` references an existing service.
  - operation `train ID` references an existing train.
- Train times satisfy: `a_t >= 0` and `d_t >= a_t`.
- The number of `service*` columns in `Node types.csv` must match the number of services in `Service input.csv`.
- A given service cannot be assigned more than once to the same train.

## Service name mapping in the provided instances

The service column headers in `Node types.csv` follow the service order defined in each station's `scenario N/Service input.csv`.
Below is the mapping from `Node types.csv` service columns to the service names defined in the scenarios.

**small station** (scenarios 1–5 share the same services)

| Node types column | Service name |
| --- | --- |
| service1 | Repair |
| service2 | Wheel-turning |
| service3 | Cleaning-1 |
| service4 | Cleaning-2 |

**variant station** (scenario 1)

| Node types column | Service name |
| --- | --- |
| ES | ES |
| NSN-NS2 | NSN-NS2 |
| WC-1 | WC-1 |
| WC-2 | WC-2 |
| WC-3 | WC-3 |

## Validation checklist

- All required files exist for each instance and scenario.
- CSV headers match the documented schemas exactly.
- IDs are contiguous and ordered from 1..N.
- Every foreign key references an existing row.
- Arrival/departure times are valid (`a_t >= 0` and `d_t >= a_t`).
- Node types include the correct number of `service*` columns.

## Visualization note

The plotting helper `python/visualize_results.py` reads station structure
directly from these CSVs (`Node input.csv`, `Link input.csv`, `Node types.csv`),
in addition to the solver output logs.
