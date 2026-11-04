# Link conflicts and the ALNS method

This note explains the two changes of this branch: the **link-conflict constraint**
(the model) and the **Adaptive Large Neighbourhood Search** (a second solve method). It also
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

## 2. Adaptive Large Neighbourhood Search (`--method lns`)

The ALNS reuses the B&P machinery (pricing, Lagrangian bound, column pool). It is a primal
method: it finds good solutions fast, but its only lower bound is the root LP bound, so it
proves optimality only when UB reaches that bound.

```
root      greedy insertion (several orders)                       → first solution in milliseconds
          column generation on all trains (time-budgeted)        → root LP bound LB + root duals
          LP-guided greedy insertion, then MIP on the root columns → initial current solution
          (root MIP ≤ min(lns_root_mip_time_s, 20 % of the time limit))
loop      draw the acceptance threshold θ (simulated annealing)
          destroy : pick an operator (roulette on weights), free k trains; the others keep their path
          repair  : pick a repair operator (roulette on weights):
                    greedy   — insert the freed trains one by one through the pricing
                    regret   — insert first the train whose refusal would cost the most
                    dive     — CG on a reduced sub-master + diving, pruned by its bound
                    dive_mip — dive, then a short MIP on the sub-master's columns
          accept  : new best | better than current | worse but ≤ θ (SA) | rejected
          every lns_segment iterations: update the operator weights
          stop    : time limit, iteration limit, or UB = ⌈LB⌉ (proven optimal)
```

### Reduced sub-master (`SubMaster`)
The first LNS re-solved the **full** restricted master (all trains, all conflict rows)
for every neighbourhood, with the fixed trains held by column bounds. Each LP solve cost
as much as at the root, so only 1–26 neighbourhoods fitted in 60 s.

A neighbourhood now gets its own small LP:
- **columns:** only the freed trains — their refusal column, their current path and their
  300 most recent pool columns, plus whatever the neighbourhood's pricing generates;
- **fixed trains are constants:** their cost is added to the objective, and the
  (resource, time) cells they occupy are **blocked**: a column using a blocked cell is
  never added, and the pricing sees μ = −BIG on those cells;
- **conflict rows are created lazily**, only for cells that at least two columns of the
  sub-master use.

The sub-master has typically a few hundred rows and columns (the app shows
"rows × cols" per iteration) instead of thousands. Its Lagrangian bound
`z_sub + cost(fixed) + Σ_{k freed} min(0, rc_k)` is a valid bound of the neighbourhood,
because the fixed trains contribute constants and no reduced cost.

### Repair operators
| repair | what it does | cost |
| --- | --- | --- |
| `greedy` | refused trains first, then by LP rank (randomised); each insertion is one pricing call with π_k = refusal cost, μ = −BIG on used cells → the cheapest conflict-free path doing all the services, or a refusal | ms |
| `regret` | prices every remaining train, inserts the one with the largest refusal-cost − path-cost (+ noise), re-prices lazily only the trains whose path touched the newly used cells | ms |
| `dive` | CG on the sub-master; while the LP is fractional, fix the columns ≥ 0.99 (or the largest one) and re-price; a refusal column is never rounded | 0.1–2 s |
| `dive_mip` | `dive`, then a MIP on all the sub-master's columns with cutoff min(current, dive) − 1 | ≤ `lns_mip_time_s` more |

**Bound pruning:** costs are integers, so the LP repairs stop as soon as the neighbourhood
bound is ≥ current − 1 ("pruned" if it happens before any fixing, "dive cut" after).
LP repairs therefore only search for improvements of the *current* solution; the cheap
insertions are the ones that produce the worse moves that simulated annealing may accept.

**Iteration budget:** an LP repair stops at `max(1 s, lns_iter_time_frac × time limit)`
(3 % by default). Without it, a single degenerate neighbourhood CG could take 30 s on
small sce 5.

### Destroy operators
| operator | frees |
| --- | --- |
| `random` | k random trains |
| `time` | the trains present in the station at the same time as a random seed train |
| `related` | the trains sharing physical nodes with a seed train while present at the same time |
| `dual` | high reduced cost of their current path under the root duals (randomised greedy, Ropke & Pisinger) |
| `rejected` | refused trains + the trains competing with them for the same time window |
| `bottleneck` | the trains whose current path uses a cell the root LP prices as most contested (most negative conflict dual μ, roulette on −μ over the 200 most contested cells) |

### Acceptance: simulated annealing with a relative temperature
The temperature is relative to the **current** cost: a solution worse by a fraction p of
the current cost is accepted with probability 1/2, with p decreasing geometrically from
`lns_sa_start` (2 %) to `lns_sa_end` (0.05 %) along the time (or iteration) budget:

```
T = p(progress) · cost(current) / ln 2
θ = cost(current) − T · ln U,  U ~ U(0,1)        (drawn before the repair)
accept the candidate if cost ≤ θ
```
`--lns-accept improve` sets θ = current − 1 (pure descent). The best solution is always
kept apart from the current one, and a visited-solution hash avoids rewarding cycles.

### Adaptive weights (Ropke & Pisinger segments)
Each iteration scores both operators used: **33** for a new best, **9** for better than
current, **1** for a worse solution accepted for the first time, 0 otherwise. Every
`lns_segment` iterations (10):

```
w ← max(0.1, (1 − ρ) w + ρ · perf / mean(perf))       ρ = lns_reaction = 0.3
perf = score / use                 (destroy operators)
perf = score / (time + 0.05 · use) (repair operators: their costs differ by 1000×)
```
Operators not used in the segment keep their weight.

**Neighbourhood size** (share of freed trains, from `lns_init_frac` to `lns_max_frac`):
×1.1 when pruned or after every 5 iterations without a new best, ×0.85 when the MIP times
out or the iteration budget is spent, and back to `lns_init_frac` after 15 non-improving
iterations at the maximum (diversification).

### Degeneracy
Neighbourhood sub-masters can be strongly degenerate: the RMP value stays constant while
the pricing keeps finding negative reduced costs. The CG stops after 25 iterations without
RMP improvement (60 at the root), or at the iteration budget; the bound computed so far
stays valid. Dual stabilisation (Wentges smoothing) would be the next step.

### Measured effect (CLP/CBC adapter, 60 s, 2 seeds, link conflicts on)
| instance | best root LB | first LNS (mean UB) | ALNS (mean UB) | iterations / 60 s (old → new) |
| --- | ---: | ---: | ---: | --- |
| medium sce 2 | 10,662 | 44,010 | **23,121** | 1–4 → 56–62 |
| small sce 2 | 5,438 | 16,211 | **12,359** | 17–26 → 66–81 |
| small sce 4 | 25,347 | 48,662 | **43,357** | 1–2 → 46–66 |
| small sce 5 | 25,386 | 101,396 | **95,099** | 3–5 → 37–93 |

On small sce 2 the ALNS mean (12,359) is below the B&P incumbent after the same time
(12,504). Successful repairs are spread over all four repair operators, which is what the
per-second scoring is meant to achieve. With CLP/CBC every LP is several times slower
than with Gurobi, so the time-related parameters (iteration budget, MIP time, root time
share) should be recalibrated on the Gurobi machine; the app's "Fast", "Intensify" and
"Large" presets are starting points for that.

### Trace and app
The ALNS writes the same `trace.jsonl` events as the B&P (`cg` with `ctx` = `root` / `lns`,
`bounds`, `incumbent` with sources such as `greedy:lp-guided`, `lns:dual+greedy`,
`lns:bottleneck+dive_mip`), plus one `lns` event per iteration: destroy and repair
operator, k, neighbourhood bound, candidate / previous / current / best cost, temperature,
threshold, result, CG iterations, dive steps, sub-master size and both weight vectors.
In the app:
- the Convergence tab shows the search trajectory (each candidate coloured by its
  outcome, the current solution, the best solution and the root bound), outcomes per
  destroy and per repair operator, the repair operators' time share, and the evolution
  of the weights, of k and of the temperature;
- the run dialog exposes every parameter, with presets;
- the Benchmark page compares B&P and ALNS per instance, with the gap measured to the
  best lower bound known for that instance.

Runs of the first LNS (results `improved` / `no_improvement` / `dive_cut` / `mip_timeout`)
are still displayed.

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
