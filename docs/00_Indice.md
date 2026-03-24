# K-means Paralelo - Índice de Notas

1. [[01_Arquitectura]]
2. [[02_Algoritmo_KMeans]]
3. [[03_Paralelizacion_OpenMP]]
4. [[04_Flujo_y_CLI]]
5. [[05_Datos_CSV_y_Scripts]]
6. [[06_Experimentos_y_Resultados]]
7. [[07_Modulos_y_Codigo]]

## Mapa conceptual general

```mermaid
flowchart TD
    A[Usuario / Script] --> B[CLI bin/kmeans]
    B --> C[Lectura de CSV]
    C --> D[Core compartido de K-means]
    D --> E[Backend serial]
    D --> F[Backend OpenMP]
    E --> G[Centroides y assignments]
    F --> G
    G --> H[CSV de salida]
    G --> I[CSV de experimentos]
    I --> J[Speedup y graficas]
    J --> K[Reporte y analisis]
```

## Capas del proyecto

```mermaid
flowchart LR
    subgraph UX[Interfaz]
        CLI[src/main.c]
    end

    subgraph IO[Entradas / Salidas]
        CSV[src/csv_io.c]
        TIME[src/time_utils.c]
        RNG[include/rng.h]
    end

    subgraph CORE[Nucleo Algoritmico]
        KCORE[src/kmeans_common.c]
        KINT[include/kmeans_core.h]
    end

    subgraph BACKENDS[Backends]
        KS[src/kmeans_serial.c]
        KO[src/kmeans_omp.c]
    end

    subgraph EXP[Experimentacion]
        RUN[scripts/run_experiments.ps1 /.sh]
        PLOT[scripts/plot_speedup.py]
        GEN[scripts/generate_synthetic.py]
        REPORT[docs/REPORT.md]
    end

    CLI --> CSV
    CLI --> KCORE
    KCORE --> KS
    KCORE --> KO
    CLI --> TIME
    KCORE --> RNG
    RUN --> CLI
    RUN --> GEN
    RUN --> PLOT
    PLOT --> REPORT
```


