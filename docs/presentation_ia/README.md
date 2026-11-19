# Présentation : Branch-and-Price, ALNS et apport de l'IA agentique (10 slides + bonus, thème metropolis)

`main.pdf` est la présentation compilée, `main.tex` sa source Beamer.

Compilation (TeX Live avec beamer, pgfplots, tikz) :

```bash
pdflatex main.tex && pdflatex main.tex
```

- Le tableau des résultats (slide 8) reprend l'export du benchmark de l'application
  (`tusp_bp_vs_lns_202611181432.csv`, Gurobi).
- `data/cg_root_s1.csv` : génération de colonnes à la racine, petite gare, scénario 1.
- `data/crossings.csv` : croisements dans les solutions du modèle d'origine.
- `img/` : captures de l'application.
