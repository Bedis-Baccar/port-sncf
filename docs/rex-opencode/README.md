# L’IA agentique au quotidien — REX OpenCode & bonnes pratiques (Beamer)

`main.pdf` is the 21-slide deck (16:9); `notes.pdf` is the same deck with the speaker notes
on a second screen (right half of each page, for a presenter tool such as `pdfpc` or dual-screen PDF viewers).

Build (TeX Live with beamer, pgfplots, TikZ and the `plex` font package; XeLaTeX):

```bash
latexmk -xelatex main.tex
latexmk -xelatex notes.tex
```

- Fonts: IBM Plex Sans / Mono come from TeX Live (`plex`); the Archivo headings are bundled in `fonts/`
  (SIL Open Font License 1.1, see `fonts/OFL-Archivo.txt`).
- Slide modes: `\light` (paper background, blue strip), `\dark` (navy, lime strip) and `\limeslide`;
  `\kicker{01}{Le terrain}` sets the numbered eyebrow above a frame title; `\actslide{…}` draws an act divider.
- Building blocks are in the preamble: `\cardbox[style]{width}{height}{…}` with `card`, `dcard` (dark),
  `ncard` / `hcard` (on dark), `bcard` (blue outline); `\ctag`, `\ctitle`, `\cbody`, `\pull` (quote with a rule),
  `\chip`, `\code`.
- Speaker notes are the `\note{…}` after each frame.
