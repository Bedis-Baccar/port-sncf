# Link conflicts and the LNS method

This note explains the two changes of this branch: the **link-conflict constraint**
(the model) and the **Large Neighbourhood Search** (a second solve method). It also
says how each was verified.

## 1. Link conflicts

### The problem
The original model only forbids conflicts on **nodes**. A shunting arc `p → q` that starts
at `t₁` only occupies its departure node `p` at `t₁` (headway `t_h`), and nothing reserves
the link during the move. Two trains standing at `p` and `q` can leave at the same time in
opposite directions. Each reaches the other's node at `t₁ + d` without ever sharing a node
in the same time step, so they cross each other on the same track. That is physically
impossible: `pX-1 ↔ pX-2` are the two ends of a single storage track.

Across the 10 results of the original solver there are **89 such crossings**: 34 inside a
single track (`X-1 ↔ X-2`) and 55 between two sections.

### The constraint
One capacity-1 resource per physical link ℓ = {p, q} (an **unordered** pair, so a link
listed in both directions is one track) and per time step τ. Every shunting arc on ℓ, in
either direction, uses it over `[t₁, t₁ + d)`:

```
Σ_k Σ_{c ∈ Ω_k}  b_{ℓτ}^c · λ_c  ≤ 1        ∀ ℓ ∈ L, ∀ τ ∈ {0, t_s, …, T}
```

Here `b_{ℓτ}^c` (0 or 1) says whether path `c` has a shunting arc on ℓ covering τ.
Same-direction conflicts were already excluded by the headway at the departure node, so
the new rows only forbid **opposite** moves. Trains following each other through a section
stay allowed.

### Implementation
The link resources are extra entries of each arc's occupation list `Arc::iNodes`, with
resource ids `|P| … |P| + |L| − 1` (see `GraphBuilder::compute_link_resources`). The
master's coefficients (`MasterProblem::add_column`) and the pricing's penalties
(`PricingProblem::get_arc_reduced_cost`) are both built from this one list, so the
reduced cost stays exact and the pricing algorithm is unchanged. Only `GraphBuilder`,
`TimeSpaceGraph` (number of resources) and the sizing of the conflict rows in
`MasterProblem` changed. Arc ids are unchanged, so old results still decode.

`--no-link-conflicts` (or the checkbox in the app) gives back the original model.

### Cost
- **Rows:** `|L| · (T/t_s + 1)` more conflict rows in the master (×2.7 to ×5), almost all
  slack. Small station sce 1 goes from 24 to 65 conflict resources.
- **Density:** one more nonzero per move in each column.
- **Pricing:** the graph is unchanged.
- **Root bound:** on every small instance tested, the root LP bound was **identical** with
  and without the constraint. Crossings only appear in integer solutions, so the price is
  paid in branching. Example (CLP/CBC, see §3): small sce 1 is proven optimal in ~4 s
  without the constraint, but still has a 0.46 % gap after 90 s with it.
- **Objective:** it can only stay equal or increase. Results of the two models are not
  directly comparable.

## 2. Large Neighbourhood Search (`--method lns`)

The LNS reuses the B&P machinery (restricted master, pricing, Lagrangian bound). It is a
primal method: it finds good solutions fast, but its only lower bound is the root LP
bound, so it does not prove optimality except when UB reaches that bound.

```
root      greedy insertion (several orders)                       → first solution in milliseconds
          column generation on all trains (time-budgeted)        → root LP bound LB (valid)
          LP-guided greedy insertion, then MIP on the root columns → better solution
loop      destroy : free k trains (adaptive operator, adaptive k); the others keep their path
          repair  : 1. greedy re-insertion of the freed trains through the pricing
                    2. price-and-dive: CG pricing only the freed trains → neighbourhood bound;
                       fix the most integral columns, re-price, … until the LP is integral
                    3. (optional) short MIP on the neighbourhood's columns, cutoff UB − 1
          stop    : time limit, or UB = ⌈LB⌉ (proven optimal)
```

### How the pricing and the relaxation are used
- **Greedy insertion is a pricing call.** Train k is priced with π_k = its refusal cost,
  α = 0, and μ = −BIG on every resource already used. The pricing then returns exactly the
  cheapest conflict-free path that performs all the train's services, or nothing (refusing
  the train is cheaper). This builds a full feasible solution in about n pricing calls,
  with no MIP. It is used for the first solution and as the fast repair.
- **Neighbourhood bound.** In a neighbourhood, a fixed train has a single column, already
  in the RMP, so its best reduced cost is ≥ 0. Hence `z_RMP + Σ_k min(0, rc_k)` is a
  valid lower bound of the restricted problem. Costs are integers, so if this bound is
  ≥ UB − 1 the neighbourhood cannot contain a better solution: its CG stops at once and
  the repair is skipped ("pruned"). The same test cuts a dive as soon as the fixings make
  improvement impossible ("dive cut").
- **Root bound:** it gives the gap, and `UB = ⌈LB⌉` proves optimality.
- **Dual-guided destroy:** it frees the trains whose current path has the largest reduced
  cost under the root duals, i.e. the paths the LP disagrees with most.
- **LP-guided insertion:** it inserts first the trains with the smallest LP flow on their
  "refused" column.
- **MIP cutoff UB − 1:** the optional MIP polish only searches for strict improvements.

### Destroy operators (ALNS)
| operator | frees |
| --- | --- |
| `random` | k random trains |
| `time` | the trains present in the station at the same time as a random seed train |
| `related` | the trains sharing physical nodes with a seed train while present at the same time |
| `dual` | high reduced cost under the root duals (randomised greedy, Ropke & Pisinger style) |
| `rejected` | refused trains + the trains competing with them for the same time window |

**Operator weights:** `w ← 0.8 w + 0.2 score`, where the score is 6 for a new best, 1 for
"repaired but no better", 0.5 for a MIP timeout and 0.3 for pruned.

**Neighbourhood size:** the share of freed trains grows (×1.15) when neighbourhoods are
pruned or nothing improves, shrinks (×0.85) when the MIP times out, and restarts at its
initial value after 12 non-improving iterations at the maximum (diversification).

### Degeneracy
Restricted masters of neighbourhoods can be strongly degenerate: the RMP value stays
constant while the pricing keeps finding negative reduced costs. The CG stops after 25
iterations without RMP improvement (60 at the root); the bound computed so far stays
valid. Dual stabilisation (e.g. Wentges smoothing) would be the next step.

### Trace and app
The LNS writes the same `trace.jsonl` events as the B&P (`cg` with `ctx` = `root` / `lns`,
`bounds`, `incumbent` with sources such as `greedy:lp-guided`, `lns:dual+greedy`,
`lns:time`, `lns:random+mip`), plus one `lns` event per iteration (operator, k,
neighbourhood bound, result, weights). In the app:
- the Convergence tab shows the neighbourhoods against UB and the root bound, the
  outcomes per operator, the adaptive weights and the neighbourhood size;
- the Benchmark page compares B&P and LNS per instance, with the gap measured to the best
  lower bound known for that instance.

## 3. Verification

The solver was run in a development container **without Gurobi**, through a test-only
adapter implementing the Gurobi C++ API on COIN-OR CLP (LP + duals) and CBC (MIP). The
adapter's duals follow Gurobi's sign convention (checked on a small LP). It reproduces the
root bounds of the Gurobi benchmark exactly (small sce 1 to 5: 1959.5, 5438.2, 20534.2,
27762.2, 63822.9) and the small sce 1 optimum (1965, original model).

- **Graph:** the C++ graph and the Python rebuild used by the app are identical on all 20
  instances (arcs, costs, occupations, number of resources), with and without link
  conflicts.
- **Reduced costs:** with `-DTUSP_CHECK_REDUCED_COSTS`, every column returned by the
  pricing has its reduced cost recomputed from the master's duals and coefficients.
  66 548 columns were checked across the runs, with no mismatch.
- **Solutions:** every solution produced with link conflicts was re-checked by the
  decoder: 0 node conflicts, 0 crossings, path costs summing to the objective. The same
  instances under the original model have 2, 5, 7 and 5 crossings.
- **Old results:** the decoder's checker counts exactly the 89 crossings in them
  (`app/backend/tests`).

**Performance comparisons between B&P and LNS must be made with Gurobi.** CLP/CBC is much
slower, especially for the many small LP/MIP re-solves of the LNS, and CBC overruns its
time limits. Use the app's Benchmark page ("Run a benchmark") or `benchmark_tusp_solver`
with `method = "lns"`.
