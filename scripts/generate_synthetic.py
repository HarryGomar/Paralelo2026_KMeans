#!/usr/bin/env python3
import argparse
import csv
import math
import random


def main() -> int:
    p = argparse.ArgumentParser(description="Generate synthetic clustered points (CSV).")
    p.add_argument("--dim", type=int, choices=[2, 3], required=True)
    p.add_argument("--n", type=int, required=True)
    p.add_argument("--k", type=int, default=5)
    p.add_argument("--seed", type=int, default=123)
    p.add_argument("--std", type=float, default=1.0, help="Gaussian stddev per cluster")
    p.add_argument("--spread", type=float, default=10.0, help="Range for random cluster centers")
    p.add_argument("--out", required=True)
    args = p.parse_args()

    rng = random.Random(args.seed)
    dim = args.dim
    n = args.n
    k = args.k

    centers = []
    for _ in range(k):
        centers.append([rng.uniform(-args.spread, args.spread) for _ in range(dim)])

    # Distribute points roughly evenly among clusters.
    base = n // k
    rem = n % k

    header = ["x", "y"] if dim == 2 else ["x", "y", "z"]
    with open(args.out, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(header)
        for c in range(k):
            cnt = base + (1 if c < rem else 0)
            cx = centers[c]
            for _ in range(cnt):
                row = [rng.gauss(cx[d], args.std) for d in range(dim)]
                w.writerow([f"{v:.10f}" for v in row])

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

