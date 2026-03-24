#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

mkdir -p data results

MAKE_CMD="${MAKE:-make}"

command -v "$MAKE_CMD" >/dev/null 2>&1 || {
  echo "ERROR: no se encontro '$MAKE_CMD' en PATH"
  exit 1
}
command -v python3 >/dev/null 2>&1 || {
  echo "ERROR: no se encontro python3 en PATH"
  exit 1
}

K="${K:-5}"
MAX_ITERS="${MAX_ITERS:-300}"
TOL="${TOL:-1e-4}"
RUNS="${RUNS:-10}"

# Improve repeatability/perf on many systems (can be overridden by env).
export OMP_PROC_BIND="${OMP_PROC_BIND:-close}"
export OMP_PLACES="${OMP_PLACES:-cores}"

VCORES="$(nproc)"
HALF="$((VCORES/2))"
if [[ "$HALF" -lt 1 ]]; then HALF=1; fi

# Default thread grid (unique, >=1)
THREADS_DEFAULT=(1 "$HALF" "$VCORES" "$((2*VCORES))")
THREADS=()
for t in "${THREADS_DEFAULT[@]}"; do
  if [[ "$t" -lt 1 ]]; then continue; fi
  if [[ " ${THREADS[*]} " != *" $t "* ]]; then THREADS+=("$t"); fi
done

if [[ -n "${NS_LIST:-}" ]]; then
  read -r -a NS <<< "${NS_LIST}"
else
  NS=(100000 200000 300000 400000 600000 800000 1000000)
fi

if [[ -n "${DIMS_LIST:-}" ]]; then
  read -r -a DIMS <<< "${DIMS_LIST}"
else
  DIMS=(2 3)
fi

if [[ -n "${THREADS_LIST:-}" ]]; then
  read -r -a THREADS <<< "${THREADS_LIST}"
fi

RESULTS_CSV="${RESULTS_CSV:-results/experiments.csv}"
rm -f "$RESULTS_CSV"
EXPECTED_ROWS=$(( ${#DIMS[@]} * ${#NS[@]} * RUNS * (1 + ${#THREADS[@]}) ))

echo "Building..."
"$MAKE_CMD" -j

echo "Capturing HW/SW info..."
{
  echo "date: $(date -Iseconds)"
  echo "uname: $(uname -a)"
  echo "nproc: $(nproc)"
  command -v lscpu >/dev/null && lscpu || true
  echo
  echo "compiler:"
  ${CC:-gcc} --version 2>/dev/null || true
} > results/system_info.txt

echo "Running experiments:"
echo "  K=$K MAX_ITERS=$MAX_ITERS TOL=$TOL RUNS=$RUNS"
echo "  VCORES=$VCORES THREADS=${THREADS[*]}"
echo "  Expected rows=$EXPECTED_ROWS"

for dim in "${DIMS[@]}"; do
  for N in "${NS[@]}"; do
    DATA="data/synth_${dim}d_N${N}.csv"
    if [[ ! -f "$DATA" ]]; then
      echo "Generating $DATA ..."
      python3 scripts/generate_synthetic.py --dim "$dim" --n "$N" --k "$K" --seed 123 --out "$DATA"
    fi

    for ((run=0; run<RUNS; run++)); do
      SEED="$((42 + run))"

      # Serial baseline
      ./bin/kmeans --input "$DATA" --dim "$dim" --k "$K" --mode serial \
        --max-iters "$MAX_ITERS" --tol "$TOL" --seed "$SEED" \
        --log-csv "$RESULTS_CSV" --run-idx "$run" >/dev/null

      # OpenMP runs
      for t in "${THREADS[@]}"; do
        ./bin/kmeans --input "$DATA" --dim "$dim" --k "$K" --mode omp --threads "$t" \
          --max-iters "$MAX_ITERS" --tol "$TOL" --seed "$SEED" \
          --log-csv "$RESULTS_CSV" --run-idx "$run" >/dev/null
      done
    done
  done
done

python3 - "$RESULTS_CSV" "$RUNS" "$EXPECTED_ROWS" <<'PY'
import csv
import sys
from collections import Counter

path = sys.argv[1]
runs = int(sys.argv[2])
expected_rows = int(sys.argv[3])

with open(path, newline="") as f:
    rows = list(csv.DictReader(f))

if not rows:
    raise SystemExit(f"ERROR: {path} no contiene filas")

if len(rows) != expected_rows:
    raise SystemExit(f"ERROR: {path} tiene {len(rows)} filas, se esperaban {expected_rows}")

counts = Counter((r["dim"], r["N"], r["mode"], r["threads"]) for r in rows)
bad = {key: count for key, count in counts.items() if count != runs}
if bad:
    for key, count in sorted(bad.items()):
        print(f"ERROR: configuracion {key} tiene {count} filas, se esperaban {runs}")
    raise SystemExit(1)

print(f"Validated {len(rows)} rows in {path}")
PY

echo "Done: $RESULTS_CSV"
echo "Next:"
echo "  python3 scripts/plot_speedup.py --input $RESULTS_CSV --outdir results"
