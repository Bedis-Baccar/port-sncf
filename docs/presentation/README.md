# Project presentation (20 slides)

`main.pdf` is the compiled deck; `main.tex` its Beamer source.

Build (TeX Live with beamer, pgfplots, tikz):

```bash
pdflatex main.tex && pdflatex main.tex
```

- `data/` — CSV series plotted with pgfplots, extracted from the solver traces:
  `cg_root_s1.csv` (root column generation, small 1), `bp_gurobi_s2.csv` (original Gurobi run, small 2),
  `lns_s1_*.csv` (LNS neighbourhoods and incumbents, small 1), `crossings.csv` (head-on crossings in the
  10 Gurobi solutions of the original model).
- `img/` — screenshots of the TUSP Visualizer app.

Numbers labelled "CLP/CBC test solver" come from runs made without Gurobi (open-source LP/MIP
solvers behind the Gurobi C++ API); re-run them with Gurobi through the app's benchmark launcher.
