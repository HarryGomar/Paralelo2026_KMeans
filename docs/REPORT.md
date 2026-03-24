# K-means con OpenMP — Estrategia y Evaluación

## 1) Descripción del código

El algoritmo implementa K-means clásico:

1. Inicialización de centroides: se eligen `k` puntos del dataset de manera aleatoria **reproducible** con `--seed`.
2. Iteración:
   - **Asignación**: cada punto se asigna al centroide con menor distancia euclidiana (usamos distancia al cuadrado).
   - **Actualización**: cada centroide se actualiza como el promedio de los puntos asignados.
3. Convergencia: se detiene cuando:
   - `changed == 0` (ningún punto cambió de cluster), o
   - `max_shift < tol` (máximo desplazamiento L2 de centroides menor que `--tol`),
   - o se alcanza `--max-iters`.

Casos especiales:
- **Cluster vacío**: si un centroide queda con 0 puntos asignados, se re-inicializa seleccionando un punto aleatorio del dataset (también reproducible con `--seed`).

Entrada/salida:
- Entrada: CSV con 2 o 3 columnas numéricas separadas por comas, con encabezado opcional.
- Salida (opcional): CSV por punto `x,y[,z],cluster` y CSV de centroides `cluster,cx,cy[,cz]`.

## 2) Estrategia de paralelización (OpenMP)

La paralelización se centra en la fase más costosa: **asignación de puntos**.

- Se paraleliza el loop sobre puntos `i = 0..N-1` usando `#pragma omp for schedule(static)`.
- Para evitar *data races* y contención en la actualización de sumas y conteos por cluster:
  - Cada hilo mantiene **acumuladores privados**: `local_sums[k][dim]` y `local_counts[k]`.
  - Al final de la región paralela se realiza una **reducción manual** en serie: se suman los acumuladores por hilo para obtener `global_sums` y `global_counts`.
- Se evita `critical`/`atomic` por punto (lo cual degradaría el rendimiento).

## 3) Definición del experimento de rendimiento

Parámetros del experimento (según la especificación del proyecto):

- Dimensión: `dim ∈ {2, 3}`
- Número de puntos: `N ∈ {100000, 200000, 300000, 400000, 600000, 800000, 1000000}`
- Hilos: `threads ∈ {1, vcores/2, vcores, 2*vcores}` (y opcionalmente explorar más/menos)
- Repeticiones: 10 corridas por configuración (`run_idx = 0..9`)
- Comparación: `mode=serial` vs `mode=omp`

Medición de tiempo:
- `kernel_ms`: tiempo de cómputo de K-means (sin I/O), medido con `clock_gettime(CLOCK_MONOTONIC, ...)`.
- `total_ms`: tiempo total incluyendo lectura del CSV y (si se usa) escritura de outputs.

Datos y gráficas:
- `scripts/run_experiments.sh` genera `results/experiments.csv` con columnas:
  `dim,N,k,mode,threads,run_idx,iters,kernel_ms,total_ms`
- `scripts/plot_speedup.py` calcula:
  `speedup = mean(kernel_ms_serial) / mean(kernel_ms_omp)`
  y produce PNGs por dimensión.

## 4) Equipo HW/SW y análisis

El script guarda información del sistema en `results/system_info.txt` (CPU, cores virtuales, kernel, compilador).

Interpretación esperada:
- El speedup depende de `N`, `k`, `dim` y del costo relativo del cómputo vs overheads (memoria, sincronización, reducción).
- En configuraciones representativas (p.ej. `N` grande), se espera observar `speedup >= 1.5` en algún número de hilos, cumpliendo la nota del proyecto.

