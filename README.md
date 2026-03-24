# K-means Paralelo en C (Serial + OpenMP)

Proyecto de K-means clasico con dos modos de ejecucion:

- `serial`
- `omp` con OpenMP

La interfaz publica se mantiene en un solo ejecutable, `bin/kmeans`, pero el codigo interno quedo
reorganizado para separar con claridad:

- CLI e integracion con archivos CSV
- nucleo compartido del algoritmo
- backend serial
- backend OpenMP

## Estructura

- `src/main.c`: parsing de argumentos, carga de datos, ejecucion y salida.
- `src/kmeans_common.c`: flujo comun de K-means, inicializacion y actualizacion de centroides.
- `src/kmeans_serial.c`: asignacion y acumulacion en modo serial.
- `src/kmeans_omp.c`: asignacion y acumulacion paralela con acumuladores privados por hilo.
- `src/csv_io.c`: lectura y escritura de CSV.
- `scripts/run_experiments.sh`: grid experimental completo y validacion del CSV final.
- `scripts/plot_speedup.py`: genera el resumen de speedup y las graficas PNG.
- `docs/REPORT.md`: reporte tecnico y experimental.

## Compilacion

```bash
make
```

Genera:

```bash
bin/kmeans
```

En Windows nativo con PowerShell + MSYS2 UCRT64:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build_windows.ps1
```

Genera:

```text
bin\kmeans.exe
```

## Uso

Serial:

```bash
./bin/kmeans --input data.csv --k 5 --mode serial --max-iters 300 --tol 1e-4 --seed 42 \
  --out results/out.csv --centroids results/centroids.csv
```

OpenMP:

```bash
./bin/kmeans --input data.csv --k 5 --mode omp --threads 8 --max-iters 300 --tol 1e-4 --seed 42 \
  --out results/out.csv --centroids results/centroids.csv
```

Opciones principales:

- `--input`: CSV de entrada con 2 o 3 columnas numericas.
- `--k`: numero de clusters.
- `--mode`: `serial` u `omp`.
- `--threads`: numero exacto de hilos en modo `omp`.
- `--dim`: `2` o `3` si se quiere forzar la dimension.
- `--out`: CSV con puntos etiquetados por cluster.
- `--centroids`: CSV con centroides finales.
- `--log-csv`: agrega una fila al CSV de experimentos.
- `--run-idx`: indice de repeticion para el experimento.

## Experimentos

El script experimental ejecuta la malla pedida por el proyecto:

- dimensiones: `2` y `3`
- numero de puntos: `100000 200000 300000 400000 600000 800000 1000000`
- hilos: `1`, `vcores/2`, `vcores`, `2*vcores`
- repeticiones: `10`

Ejecutar en Linux/WSL:

```bash
./scripts/run_experiments.sh
```

Ejecutar en Windows nativo:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\run_experiments.ps1
```

Artefactos:

- `results/experiments.csv`
- `results/system_info.txt`
- `results/clusters_2d.csv`
- `results/centroids_2d.csv`
- `results/clusters_3d.csv`
- `results/centroids_3d.csv`

El script valida al final que el CSV tenga el numero esperado de filas y que cada configuracion
aparezca exactamente `RUNS` veces.

## Graficas

```bash
python3 scripts/plot_speedup.py --input results/experiments.csv --outdir results
```

Genera:

- `results/speedup.csv`
- `results/speedup_dim2.png`
- `results/speedup_dim3.png`

La definicion usada es:

```text
speedup = mean(kernel_ms_serial) / mean(kernel_ms_omp)
```

## Estrategia paralela

La fase costosa es la asignacion de puntos al centroide mas cercano. En modo OpenMP:

- se paraleliza el recorrido de los puntos con `schedule(static)`
- cada hilo mantiene acumuladores privados de sumas y conteos por cluster
- al terminar la region paralela se hace una reduccion manual hacia acumuladores globales

Esto evita `atomic` o `critical` por punto y reduce la contencion.

## Salidas CSV

Puntos etiquetados:

```text
x,y,cluster
x,y,z,cluster
```

Centroides:

```text
cluster,cx,cy
cluster,cx,cy,cz
```

## Documentacion

El documento principal para entregar esta en:

- `docs/REPORT.md`
- `docs/obsidian/00_Indice.md`
- `docs/obsidian/KMeans_Proyecto.canvas`

Ese archivo incluye descripcion del codigo, estrategia de paralelizacion, definicion del
experimento, descripcion del hardware/software, interpretacion de resultados y conclusiones.

La carpeta `docs/obsidian/` contiene una base de conocimiento mas amplia para Obsidian con:

- notas enlazadas por tema
- diagramas Mermaid
- un canvas para vista global del proyecto
