# K-means (Serial + OpenMP) — Proyecto Apertura 2026

Implementación de **K-means clásico** (asignación al centroide más cercano + actualización por promedio) en **C**:

- Versión **serial**
- Versión **paralela** con **OpenMP**
- Un solo ejecutable con CLI unificada: `--mode serial|omp`
- Scripts para correr experimentos, generar `experiments.csv` y graficar speedup

## Requisitos

- Linux
- `gcc` (o `clang`) con soporte OpenMP
- `make`
- `python3` (para scripts)
- `python3 -m pip install matplotlib` (para graficar)

## Compilar

```bash
make
```

Genera `bin/kmeans`.

## Uso (CLI unificada)

Serial:

```bash
./bin/kmeans --input data.csv --k 5 --mode serial --max-iters 300 --tol 1e-4 --seed 42 \
  --out out.csv --centroids centroids.csv
```

OpenMP:

```bash
./bin/kmeans --input data.csv --k 5 --mode omp --threads 8 --max-iters 300 --tol 1e-4 --seed 42 \
  --out out.csv --centroids centroids.csv
```

Ayuda:

```bash
./bin/kmeans --help
```

Notas:
- El CSV de entrada debe tener **2 o 3 columnas numéricas** separadas por coma (encabezado opcional).
- `--dim` es opcional (se infiere del CSV); puedes forzarlo con `--dim 2` o `--dim 3`.
- Para experimentos de rendimiento, no necesitas `--out` ni `--centroids` (así mides cómputo y no I/O).

## Experimentos de rendimiento

Ejecuta el grid (dim ∈ {2,3}, N ∈ {100k..1M}, threads ∈ {1, vcores/2, vcores, 2*vcores}) promediando 10 corridas:

```bash
./scripts/run_experiments.sh
```

Esto produce:

- `results/experiments.csv` (datos crudos de tiempos)
- `results/system_info.txt` (hardware/software)

Graficar speedup:

```bash
python3 scripts/plot_speedup.py --input results/experiments.csv --outdir results
```

Genera:

- `results/speedup.csv`
- `results/speedup_dim2.png`
- `results/speedup_dim3.png`

Personalización rápida (ejemplos):

```bash
# Menos puntos y menos repeticiones (para prueba rápida)
RUNS=3 NS_LIST="100000 200000" ./scripts/run_experiments.sh

# Explorar otros hilos
THREADS_LIST="1 2 4 8 16" ./scripts/run_experiments.sh
```

Tip: el harness fija por default `OMP_PROC_BIND=close` y `OMP_PLACES=cores` para mejorar estabilidad y rendimiento; puedes sobreescribirlos como variables de ambiente.

## Documentación

Ver `docs/REPORT.md` para:

- Estrategia de paralelización OpenMP
- Definición del experimento y análisis esperado
