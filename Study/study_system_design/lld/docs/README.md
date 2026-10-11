# docs/ — Word (.docx) versions of every LLD tutorial

70 `.docx` files, one per tutorial in `../00_foundations/`,
`../01_machine_coding_problems/`, and `../notes/`, mirroring that same
folder structure. Upload any of these to Google Drive and they open
natively as an editable Google Doc — that's what "convert to Google Doc
format" means in practice without a direct Drive connector: a real
Word file is the one format both Word and Google Docs read and write
losslessly.

## What's in them

- All 49 Mermaid diagrams across the 29 files that have them (class,
  sequence, use case, and state diagrams) are rendered as real embedded
  PNG images, not left as unrendered code — captioned "Diagram 1," "Diagram
  2," etc. within each file.
- Every table (entity/responsibility tables, quick-reference tables,
  complexity comparison tables) converts to a real Word table.
- Every code block keeps C++ syntax highlighting.
- Headings map to Word's built-in heading styles, so each document has a
  working outline/navigation pane in Word or Google Docs.

## How these were generated, if you need to regenerate one

1. Diagrams were rendered from each file's Mermaid source via
   `mmdc` (mermaid-cli) to standalone PNGs.
2. Each source `.md` file was preprocessed to replace its ` ```mermaid `
   code fences with image references pointing at those PNGs.
3. The result was converted to `.docx` via `pandoc`.

If a tutorial changes, its `.docx` here goes stale — there's no live link
between them. Regenerate the specific file with:

```bash
# from lld/, after re-rendering any changed diagrams with mmdc if the
# file's Mermaid source changed:
pandoc path/to/tutorial.md -o docs/path/to/tutorial.docx
```

(Diagram-free files can skip straight to that `pandoc` line — no
preprocessing needed.)

## The `.md` files remain the source of truth

Per your explicit choice when this was set up: **the Markdown files are
still the maintained master copy.** Everything in this program — new
problems, corrections, the whole phase-based renumbering this track went
through — happens in the `.md` files first. This `docs/` folder is a
generated, read/share convenience, regenerated from the source whenever
you want an up-to-date copy, not the other way around.
