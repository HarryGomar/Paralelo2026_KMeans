#!/usr/bin/env python3
"""
Generate an Obsidian Canvas (.canvas) "code map" from a repository.

Features:
- File nodes for source files (vault-relative paths)
- Optional group nodes for directories
- Optional edges for includes/imports (best-effort)
- Stable IDs so regeneration preserves layout (with --update-existing)
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable


DEFAULT_EXTENSIONS = (
    ".c",
    ".h",
    ".cpp",
    ".hpp",
    ".cc",
    ".py",
    ".js",
    ".jsx",
    ".ts",
    ".tsx",
    ".java",
    ".go",
    ".rs",
    ".cs",
)

DEFAULT_EXCLUDE_DIRS = (
    ".git",
    ".hg",
    ".svn",
    "node_modules",
    ".venv",
    "venv",
    "__pycache__",
    "dist",
    "build",
    "target",
    ".idea",
    ".vscode",
)


def stable_hex_id(seed: str) -> str:
    digest = hashlib.blake2b(seed.encode("utf-8"), digest_size=8).hexdigest()
    return digest  # 16 hex chars


def posix_path(path: Path) -> str:
    return path.as_posix()


def is_under(child: Path, parent: Path) -> bool:
    try:
        child.relative_to(parent)
        return True
    except ValueError:
        return False


def read_text_best_effort(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


@dataclass(frozen=True)
class CanvasNode:
    id: str
    type: str
    x: int
    y: int
    width: int
    height: int
    file: str | None = None
    text: str | None = None
    label: str | None = None
    color: str | None = None

    def to_json(self) -> dict[str, Any]:
        node: dict[str, Any] = {
            "id": self.id,
            "type": self.type,
            "x": self.x,
            "y": self.y,
            "width": self.width,
            "height": self.height,
        }
        if self.file is not None:
            node["file"] = self.file
        if self.text is not None:
            node["text"] = self.text
        if self.label is not None:
            node["label"] = self.label
        if self.color is not None:
            node["color"] = self.color
        return node


@dataclass(frozen=True)
class CanvasEdge:
    id: str
    fromNode: str
    toNode: str
    label: str | None = None
    fromSide: str | None = None
    toSide: str | None = None
    toEnd: str | None = None

    def to_json(self) -> dict[str, Any]:
        edge: dict[str, Any] = {
            "id": self.id,
            "fromNode": self.fromNode,
            "toNode": self.toNode,
        }
        if self.fromSide is not None:
            edge["fromSide"] = self.fromSide
        if self.toSide is not None:
            edge["toSide"] = self.toSide
        if self.toEnd is not None:
            edge["toEnd"] = self.toEnd
        if self.label is not None:
            edge["label"] = self.label
        return edge


def collect_files(
    root: Path,
    extensions: tuple[str, ...],
    exclude_dirs: tuple[str, ...],
    max_files: int,
) -> list[Path]:
    results: list[Path] = []
    exclude_set = set(exclude_dirs)

    for dirpath, dirnames, filenames in os.walk(root):
        dirpath_p = Path(dirpath)
        dirnames[:] = [d for d in dirnames if d not in exclude_set]
        for filename in filenames:
            if len(results) >= max_files:
                return results
            file_path = dirpath_p / filename
            if file_path.suffix.lower() in extensions:
                results.append(file_path)
    return results


def vault_relpath_for_file(
    *,
    file_path: Path,
    root: Path,
    vault_root: Path | None,
    path_prefix: str | None,
) -> str:
    rel_to_root = file_path.relative_to(root)

    if vault_root is not None:
        if not is_under(file_path, vault_root):
            raise ValueError(
                f"File is not under --vault-root: file={file_path} vault_root={vault_root}"
            )
        return posix_path(file_path.relative_to(vault_root))

    if path_prefix is not None:
        prefix = path_prefix.strip().strip("/").replace("\\", "/")
        if prefix:
            return f"{prefix}/{posix_path(rel_to_root)}"
        return posix_path(rel_to_root)

    return posix_path(rel_to_root)


def layout_grid(
    keys: list[str],
    *,
    node_width: int,
    node_height: int,
    dx: int,
    dy: int,
    max_rows: int,
) -> dict[str, tuple[int, int]]:
    positions: dict[str, tuple[int, int]] = {}
    for index, key in enumerate(keys):
        col = index // max_rows
        row = index % max_rows
        positions[key] = (col * dx, row * dy)
    return positions


INCLUDE_RE = re.compile(r'^\s*#\s*include\s*([<"])([^>"]+)[>"]', re.MULTILINE)
PY_IMPORT_RE = re.compile(
    r"^\s*(?:from\s+([a-zA-Z0-9_.]+)\s+import\s+.+|import\s+(.+))\s*$",
    re.MULTILINE,
)
JS_IMPORT_RE = re.compile(
    r'^\s*import\s+.*?\s+from\s+[\'"]([^\'"]+)[\'"]\s*;?\s*$',
    re.MULTILINE,
)
JS_REQUIRE_RE = re.compile(
    r'^\s*(?:const|let|var)\s+\w+\s*=\s*require\(\s*[\'"]([^\'"]+)[\'"]\s*\)\s*;?\s*$',
    re.MULTILINE,
)


def build_file_index(files: Iterable[Path], root: Path) -> dict[str, Path]:
    index: dict[str, Path] = {}
    for f in files:
        index[posix_path(f.relative_to(root))] = f
    return index


def resolve_c_include(
    includer: Path,
    include_target: str,
    *,
    root: Path,
    include_dirs: list[Path],
) -> Path | None:
    candidates: list[Path] = []
    candidates.append(includer.parent / include_target)
    candidates.append(root / include_target)
    for inc in include_dirs:
        candidates.append(inc / include_target)
    for c in candidates:
        if c.exists() and c.is_file():
            return c
    return None


def resolve_python_module(module: str, root: Path) -> Path | None:
    rel = Path(*module.split("."))
    file_candidate = root / (str(rel) + ".py")
    if file_candidate.exists():
        return file_candidate
    init_candidate = root / rel / "__init__.py"
    if init_candidate.exists():
        return init_candidate
    return None


def resolve_js_import(spec: str, importer: Path) -> Path | None:
    if not spec.startswith("."):
        return None
    base = (importer.parent / spec).resolve()
    candidates = []
    if base.suffix:
        candidates.append(base)
    else:
        for ext in (".js", ".jsx", ".ts", ".tsx"):
            candidates.append(Path(str(base) + ext))
        for ext in (".js", ".jsx", ".ts", ".tsx"):
            candidates.append(base / ("index" + ext))
    for c in candidates:
        if c.exists() and c.is_file():
            return c
    return None


def parse_edges(
    files: list[Path],
    *,
    root: Path,
    vault_root: Path | None,
    path_prefix: str | None,
    include_dirs: list[Path],
    edge_mode: str,
) -> list[CanvasEdge]:
    file_set = set(files)
    edges: list[CanvasEdge] = []

    vault_path_by_fs_path: dict[Path, str] = {}
    node_id_by_vault_path: dict[str, str] = {}
    for f in files:
        vault_path = vault_relpath_for_file(
            file_path=f, root=root, vault_root=vault_root, path_prefix=path_prefix
        )
        vault_path_by_fs_path[f] = vault_path
        node_id_by_vault_path[vault_path] = stable_hex_id("file:" + vault_path)

    def add_edge(from_path: Path, to_path: Path, label: str) -> None:
        from_v = vault_path_by_fs_path.get(from_path)
        to_v = vault_path_by_fs_path.get(to_path)
        if from_v is None or to_v is None:
            return
        from_id = node_id_by_vault_path[from_v]
        to_id = node_id_by_vault_path[to_v]
        edge_id = stable_hex_id(f"edge:{from_id}->{to_id}:{label}")
        edges.append(
            CanvasEdge(
                id=edge_id,
                fromNode=from_id,
                toNode=to_id,
                fromSide="right",
                toSide="left",
                toEnd="arrow",
                label=label,
            )
        )

    for f in files:
        ext = f.suffix.lower()
        content = read_text_best_effort(f)

        if edge_mode in ("auto", "includes", "both") and ext in (".c", ".h", ".cpp", ".hpp", ".cc"):
            for m in INCLUDE_RE.finditer(content):
                bracket, target = m.group(1), m.group(2).strip()
                if bracket == "<":
                    continue
                resolved = resolve_c_include(
                    f, target, root=root, include_dirs=include_dirs
                )
                if resolved is None:
                    continue
                try:
                    resolved = resolved.resolve()
                except OSError:
                    continue
                if resolved in file_set:
                    add_edge(f, resolved, "includes")

        if edge_mode in ("auto", "imports", "both") and ext == ".py":
            for m in PY_IMPORT_RE.finditer(content):
                from_mod = m.group(1)
                import_clause = m.group(2)
                modules: list[str] = []
                if from_mod:
                    modules.append(from_mod)
                if import_clause:
                    for part in import_clause.split(","):
                        part = part.strip()
                        if not part:
                            continue
                        name = part.split(" as ", 1)[0].strip()
                        if name:
                            modules.append(name)
                for mod in modules:
                    resolved = resolve_python_module(mod, root)
                    if resolved is None:
                        continue
                    resolved = resolved.resolve()
                    if resolved in file_set:
                        add_edge(f, resolved, "imports")

        if edge_mode in ("auto", "imports", "both") and ext in (".js", ".jsx", ".ts", ".tsx"):
            specs = [m.group(1).strip() for m in JS_IMPORT_RE.finditer(content)]
            specs.extend([m.group(1).strip() for m in JS_REQUIRE_RE.finditer(content)])
            for spec in specs:
                resolved = resolve_js_import(spec, f)
                if resolved is None:
                    continue
                try:
                    resolved = resolved.resolve()
                except OSError:
                    continue
                if resolved in file_set:
                    add_edge(f, resolved, "imports")

    # De-dupe by edge id (stable)
    dedup: dict[str, CanvasEdge] = {e.id: e for e in edges}
    return list(dedup.values())


def validate_canvas(nodes: list[dict[str, Any]], edges: list[dict[str, Any]]) -> None:
    ids: set[str] = set()
    for n in nodes:
        nid = n.get("id")
        if nid in ids:
            raise ValueError(f"Duplicate node id: {nid}")
        ids.add(nid)
    for e in edges:
        eid = e.get("id")
        if eid in ids:
            raise ValueError(f"Duplicate edge id (collides with node id): {eid}")
        ids.add(eid)
        fn = e.get("fromNode")
        tn = e.get("toNode")
        if fn not in {n["id"] for n in nodes}:
            raise ValueError(f"Edge fromNode missing: {fn}")
        if tn not in {n["id"] for n in nodes}:
            raise ValueError(f"Edge toNode missing: {tn}")


def read_existing_canvas(path: Path) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    data = json.loads(path.read_text(encoding="utf-8"))
    nodes = data.get("nodes") or []
    edges = data.get("edges") or []
    if not isinstance(nodes, list) or not isinstance(edges, list):
        raise ValueError("Existing canvas must contain 'nodes' and 'edges' arrays")
    return nodes, edges


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description="Generate an Obsidian Canvas code map (.canvas).")
    parser.add_argument("--root", default=".", help="Repository root to scan.")
    parser.add_argument("--out", required=True, help="Output .canvas path (ideally inside the vault).")
    parser.add_argument(
        "--vault-root",
        default=None,
        help="Obsidian vault root. Used to make file node paths vault-relative.",
    )
    parser.add_argument(
        "--path-prefix",
        default=None,
        help="Prefix to prepend to file paths in nodes (vault-relative).",
    )
    parser.add_argument(
        "--ext",
        action="append",
        default=[],
        help="File extension to include (repeatable). Example: --ext .c",
    )
    parser.add_argument(
        "--exclude-dir",
        action="append",
        default=[],
        help="Directory name to exclude (repeatable). Example: --exclude-dir results",
    )
    parser.add_argument("--max-files", type=int, default=2000)
    parser.add_argument("--node-width", type=int, default=480)
    parser.add_argument("--node-height", type=int, default=180)
    parser.add_argument("--dx", type=int, default=520, help="Grid x spacing.")
    parser.add_argument("--dy", type=int, default=220, help="Grid y spacing.")
    parser.add_argument("--max-rows", type=int, default=18, help="Max rows before wrapping to a new column.")
    parser.add_argument(
        "--groups",
        type=int,
        default=0,
        help="Create directory group nodes up to this depth (0 disables).",
    )
    parser.add_argument(
        "--edges",
        choices=("none", "auto", "includes", "imports", "both"),
        default="auto",
        help="Which relationship edges to generate.",
    )
    parser.add_argument(
        "--include-dir",
        action="append",
        default=[],
        help="Extra include search dir for resolving C/C++ includes (repeatable).",
    )
    parser.add_argument(
        "--update-existing",
        action="store_true",
        help="If --out exists, keep existing node positions/sizes when IDs match; keep unknown nodes/edges unless --prune.",
    )
    parser.add_argument(
        "--prune",
        action="store_true",
        help="When updating, remove nodes/edges not generated by this run.",
    )
    args = parser.parse_args(argv)

    root = Path(args.root).resolve()
    out = Path(args.out).resolve()
    vault_root = Path(args.vault_root).resolve() if args.vault_root else None

    extensions = tuple({e.lower() for e in (args.ext or [])} or DEFAULT_EXTENSIONS)
    exclude_dirs = tuple({d for d in (args.exclude_dir or [])} or DEFAULT_EXCLUDE_DIRS)

    include_dirs = [Path(p).resolve() for p in (args.include_dir or [])]
    for default_inc in ("include", "src", "inc"):
        candidate = root / default_inc
        if candidate.exists() and candidate.is_dir():
            include_dirs.append(candidate.resolve())

    files = [p.resolve() for p in collect_files(root, extensions, exclude_dirs, args.max_files)]
    files.sort(key=lambda p: posix_path(p.relative_to(root)))

    vault_paths: list[str] = []
    vault_path_by_file: dict[Path, str] = {}
    for f in files:
        vault_path = vault_relpath_for_file(
            file_path=f, root=root, vault_root=vault_root, path_prefix=args.path_prefix
        )
        vault_paths.append(vault_path)
        vault_path_by_file[f] = vault_path

    positions = layout_grid(
        vault_paths,
        node_width=args.node_width,
        node_height=args.node_height,
        dx=args.dx,
        dy=args.dy,
        max_rows=args.max_rows,
    )

    nodes_by_id: dict[str, dict[str, Any]] = {}
    generated_node_ids: set[str] = set()

    existing_nodes: list[dict[str, Any]] = []
    existing_edges: list[dict[str, Any]] = []
    existing_by_id: dict[str, dict[str, Any]] = {}
    if args.update_existing and out.exists():
        existing_nodes, existing_edges = read_existing_canvas(out)
        for n in existing_nodes:
            if isinstance(n, dict) and isinstance(n.get("id"), str):
                existing_by_id[n["id"]] = n

    # Directory groups (optional)
    group_nodes: list[CanvasNode] = []
    if args.groups > 0:
        members_by_group: dict[str, list[str]] = {}
        for vp in vault_paths:
            parts = vp.split("/")
            if len(parts) <= 1:
                continue
            depth = min(args.groups, len(parts) - 1)
            group_key = "/".join(parts[:depth])
            members_by_group.setdefault(group_key, []).append(vp)

        for group_key, members in sorted(members_by_group.items()):
            member_positions = [positions[m] for m in members if m in positions]
            if not member_positions:
                continue
            min_x = min(x for x, _ in member_positions) - 40
            min_y = min(y for _, y in member_positions) - 40
            max_x = max(x for x, _ in member_positions) + args.node_width + 40
            max_y = max(y for _, y in member_positions) + args.node_height + 40
            group_id = stable_hex_id("group:" + group_key)
            group_nodes.append(
                CanvasNode(
                    id=group_id,
                    type="group",
                    x=int(min_x),
                    y=int(min_y),
                    width=int(max_x - min_x),
                    height=int(max_y - min_y),
                    label=group_key,
                    color="4",
                )
            )

    for g in group_nodes:
        nodes_by_id[g.id] = g.to_json()
        generated_node_ids.add(g.id)

    for vp in vault_paths:
        node_id = stable_hex_id("file:" + vp)
        x, y = positions[vp]
        node = CanvasNode(
            id=node_id,
            type="file",
            x=int(x),
            y=int(y),
            width=int(args.node_width),
            height=int(args.node_height),
            file=vp,
        ).to_json()

        if args.update_existing and node_id in existing_by_id:
            prev = existing_by_id[node_id]
            for key in ("x", "y", "width", "height"):
                if isinstance(prev.get(key), int):
                    node[key] = prev[key]

        nodes_by_id[node_id] = node
        generated_node_ids.add(node_id)

    edges = parse_edges(
        files,
        root=root,
        vault_root=vault_root,
        path_prefix=args.path_prefix,
        include_dirs=include_dirs,
        edge_mode=args.edges,
    )
    edge_dicts = [e.to_json() for e in edges]
    generated_edge_ids = {e["id"] for e in edge_dicts}

    if args.update_existing and out.exists() and not args.prune:
        for n in existing_nodes:
            if not isinstance(n, dict):
                continue
            nid = n.get("id")
            if isinstance(nid, str) and nid not in nodes_by_id:
                nodes_by_id[nid] = n
        for e in existing_edges:
            if not isinstance(e, dict):
                continue
            eid = e.get("id")
            if isinstance(eid, str) and eid not in generated_edge_ids:
                edge_dicts.append(e)

    nodes = list(nodes_by_id.values())
    nodes.sort(key=lambda n: (0 if n.get("type") == "group" else 1, str(n.get("id"))))

    # If we kept unknown nodes, we may now have edges pointing to missing nodes.
    # Filter edges to those with valid endpoints.
    node_ids = {n["id"] for n in nodes if isinstance(n.get("id"), str)}
    filtered_edges = []
    for e in edge_dicts:
        if not isinstance(e, dict):
            continue
        if e.get("fromNode") in node_ids and e.get("toNode") in node_ids:
            filtered_edges.append(e)

    validate_canvas(nodes, filtered_edges)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps({"nodes": nodes, "edges": filtered_edges}, indent=2) + "\n", encoding="utf-8")

    print(f"[OK] Wrote {out}")
    print(f"     nodes={len(nodes)} edges={len(filtered_edges)} files_scanned={len(files)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))

