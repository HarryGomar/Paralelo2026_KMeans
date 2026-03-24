#!/usr/bin/env python3
import argparse
import csv
import os
from collections import defaultdict
from statistics import mean


def read_rows(path: str):
    with open(path, newline="") as f:
        r = csv.DictReader(f)
        for row in r:
            row["dim"] = int(row["dim"])
            row["N"] = int(row["N"])
            row["k"] = int(row["k"])
            row["threads"] = int(row["threads"])
            row["run_idx"] = int(row["run_idx"])
            row["iters"] = int(row["iters"])
            row["kernel_ms"] = float(row["kernel_ms"])
            row["total_ms"] = float(row["total_ms"])
            yield row


def main() -> int:
    p = argparse.ArgumentParser(description="Plot K-means speedup from experiments.csv")
    p.add_argument("--input", required=True, help="results/experiments.csv")
    p.add_argument("--outdir", required=True)
    p.add_argument("--metric", choices=["kernel_ms", "total_ms"], default="kernel_ms")
    args = p.parse_args()

    os.makedirs(args.outdir, exist_ok=True)

    rows = list(read_rows(args.input))
    if not rows:
        raise SystemExit("No rows found in input CSV")

    # Mean time per (dim, N, k, mode, threads)
    grouped = defaultdict(list)
    for r in rows:
        key = (r["dim"], r["N"], r["k"], r["mode"], r["threads"])
        grouped[key].append(r[args.metric])

    means = {k: mean(v) for k, v in grouped.items()}

    # Build speedup per (dim, N, k, threads): serial_mean / omp_mean
    speedup = defaultdict(dict)
    threads_set = set()
    for (dim, N, k, mode, threads), tmean in means.items():
        if mode == "omp":
            threads_set.add(threads)
            s_key = (dim, N, k, "serial", 1)
            if s_key in means and tmean > 0:
                speedup[(dim, N, k)][threads] = means[s_key] / tmean

    threads_sorted = sorted(threads_set)

    # Save a derived CSV too.
    out_csv = os.path.join(args.outdir, "speedup.csv")
    with open(out_csv, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["dim", "N", "k", "threads", "speedup"])
        for (dim, N, k), m in sorted(speedup.items()):
            for t in sorted(m.keys()):
                w.writerow([dim, N, k, t, f"{m[t]:.6f}"])

    try:
        import matplotlib.pyplot as plt
    except Exception as e:
        print(f"matplotlib no disponible ({e}). Se generó {out_csv} pero no PNG.")
        return 0

    # One figure per dimension, multiple lines for N.
    by_dim = defaultdict(list)
    for (dim, N, k), m in speedup.items():
        by_dim[dim].append((N, k, m))

    for dim, series in by_dim.items():
        plt.figure(figsize=(9, 5))
        for N, k, m in sorted(series, key=lambda x: x[0]):
            xs = [t for t in threads_sorted if t in m]
            ys = [m[t] for t in xs]
            if xs:
                plt.plot(xs, ys, marker="o", label=f"N={N}, k={k}")

        plt.title(f"Speedup K-means ({dim}D) [{args.metric}]")
        plt.xlabel("Threads")
        plt.ylabel("Speedup (serial_mean / omp_mean)")
        plt.grid(True, alpha=0.3)
        plt.legend(fontsize=8, ncol=2)
        out_png = os.path.join(args.outdir, f"speedup_dim{dim}.png")
        plt.tight_layout()
        plt.savefig(out_png, dpi=150)
        plt.close()
        print(f"Wrote {out_png}")

    print(f"Wrote {out_csv}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

