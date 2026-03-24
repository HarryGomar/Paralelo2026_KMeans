# JSON Canvas (Obsidian Canvas) quick reference

This skill writes Obsidian Canvas files (`.canvas`), which are JSON with:

```json
{ "nodes": [], "edges": [] }
```

## Node essentials

Every node needs:

- `id`: unique string (this skill uses stable 16-char lowercase hex)
- `type`: `file`, `text`, `link`, or `group`
- `x`, `y`, `width`, `height`: integers (pixels)

Common node shapes:

- **File node**: `{ "type": "file", "file": "vault/relative/path.ext" }`
- **Text node**: `{ "type": "text", "text": "Markdown text" }`
- **Group node**: `{ "type": "group", "label": "Name" }`

## Edge essentials

Every edge needs:

- `id`: unique string
- `fromNode`, `toNode`: existing node IDs

Optional:

- `fromSide` / `toSide`: `top|right|bottom|left`
- `label`: edge label

## Common pitfalls

- File paths in file nodes must be **vault-relative** to open correctly in Obsidian.
- Keep IDs stable if you plan to regenerate the canvas and preserve layout.
