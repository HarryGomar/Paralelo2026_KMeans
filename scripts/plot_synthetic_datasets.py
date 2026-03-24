#!/usr/bin/env python3
import argparse
import csv
from pathlib import Path


def read_points(path: Path) -> tuple[list[str], list[list[float]]]:
    with path.open(newline="") as f:
        reader = csv.reader(f)
        try:
            header = next(reader)
        except StopIteration as exc:
            raise SystemExit(f"{path} is empty") from exc

        rows: list[list[float]] = []
        for row in reader:
            if not row:
                continue
            rows.append([float(value) for value in row])
    return header, rows


def main() -> int:
    parser = argparse.ArgumentParser(description="Plot all synthetic 2D and 3D datasets.")
    parser.add_argument("--data-dir", default="data")
    parser.add_argument("--outdir", default="results/dataset_plots")
    parser.add_argument("--max-points", type=int, default=20000,
                        help="Maximum number of points to render per dataset")
    args = parser.parse_args()

    data_dir = Path(args.data_dir)
    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)

    csv_paths = sorted(data_dir.glob("*.csv"))
    if not csv_paths:
        raise SystemExit(f"No CSV datasets found in {data_dir}")

    try:
        import matplotlib.pyplot as plt
    except ImportError as exc:
        raise SystemExit(f"matplotlib is required: {exc}") from exc

    plotted = 0
    for csv_path in csv_paths:
        header, points = read_points(csv_path)
        dim = len(header)
        if dim not in (2, 3):
            print(f"Skipping {csv_path.name}: unsupported dimension {dim}")
            continue
        if not points:
            print(f"Skipping {csv_path.name}: no data rows")
            continue

        step = max(1, len(points) // args.max_points)
        sampled = points[::step]

        if dim == 2:
            xs = [row[0] for row in sampled]
            ys = [row[1] for row in sampled]
            fig, ax = plt.subplots(figsize=(8, 6), constrained_layout=True)
            ax.scatter(xs, ys, s=3, alpha=0.45, linewidths=0, c="#1f77b4")
            ax.set_xlabel(header[0])
            ax.set_ylabel(header[1])
        else:
            xs = [row[0] for row in sampled]
            ys = [row[1] for row in sampled]
            zs = [row[2] for row in sampled]
            fig = plt.figure(figsize=(9, 7), constrained_layout=True)
            ax = fig.add_subplot(111, projection="3d")
            ax.scatter(xs, ys, zs, s=3, alpha=0.35, linewidths=0, c="#d62728")
            ax.set_xlabel(header[0])
            ax.set_ylabel(header[1])
            ax.set_zlabel(header[2])

        ax.set_title(f"{csv_path.stem} ({len(points):,} points, showing {len(sampled):,})")
        ax.grid(True, alpha=0.25)

        out_path = outdir / f"{csv_path.stem}.png"
        fig.savefig(out_path, dpi=180)
        plt.close(fig)
        plotted += 1
        print(f"Wrote {out_path}")

    print(f"Generated {plotted} plot(s) in {outdir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
