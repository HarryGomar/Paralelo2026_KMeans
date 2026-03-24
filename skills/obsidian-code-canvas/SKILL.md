---
name: obsidian-code-canvas
description: Generate or update Obsidian Canvas (.canvas) diagrams that visualize a source-code repository as a navigable map. Use when the user asks for a “code map”, “architecture map”, “dependency graph”, “include/import graph”, or wants to browse code relationships inside Obsidian Canvas / JSON Canvas files, especially when the output should be a .canvas file with file nodes and edges.
---

# Obsidian Code Canvas

## Overview

Create `.canvas` files that turn a repo into an Obsidian Canvas: file nodes for source files, plus optional edges for `#include`/`import` relationships. Prefer using `scripts/code_to_canvas.py` to generate stable IDs so the canvas can be regenerated without losing manual layout.

## Workflow

### 1) Generate a new code map canvas

1. Decide where the `.canvas` file should live (ideally **inside your Obsidian vault** so it appears in Obsidian).
2. Run the generator:

```bash
python3 skills/obsidian-code-canvas/scripts/code_to_canvas.py \
  --root . \
  --out /path/to/your-vault/diagrams/k_means.canvas \
  --vault-root /path/to/your-vault \
  --edges auto \
  --groups 1
```

3. Open the `.canvas` file in Obsidian. Click file nodes to open code files (paths must be vault-relative).

### 2) Regenerate without losing layout

When you want to regenerate (new files, new edges) but keep your manual positions:

```bash
python3 skills/obsidian-code-canvas/scripts/code_to_canvas.py \
  --root . \
  --out /path/to/your-vault/diagrams/k_means.canvas \
  --vault-root /path/to/your-vault \
  --edges auto \
  --groups 1 \
  --update-existing
```

### 3) Fix “file node won’t open” problems

If clicking file nodes in Obsidian doesn’t open the file:

- Ensure the `.canvas` file is inside the vault.
- Ensure `--vault-root` points to the vault root directory.
- If your repo is *not* inside the vault, use `--path-prefix` to point nodes to where the code lives inside the vault (or move/symlink the code into the vault).

## What the Script Generates

- File nodes for scanned source files (vault-relative `file` paths).
- Optional group nodes for top-level directories (`--groups`).
- Optional edges:
  - C/C++: `#include "local.h"` (skips system includes like `<stdio.h>`)
  - Python: `import x` / `from x import y` (best-effort)
  - JS/TS: relative imports like `./foo` / `../bar` (best-effort)

## Common Options

- `--vault-root`: make file nodes open correctly in Obsidian (paths become vault-relative).
- `--path-prefix`: prepend a vault-relative folder prefix (useful when scanning a repo folder that will live under a subfolder in the vault).
- `--update-existing`: preserve existing node positions/sizes when regenerating.
- `--prune`: when updating, remove nodes/edges not generated this run.
- `--exclude-dir`: skip big folders (e.g. `results`, `build`, `node_modules`).

## References

- JSON Canvas essentials: `references/json_canvas_quickref.md`
