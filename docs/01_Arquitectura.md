# Arquitectura del Proyecto
## Resumen
El proyecto esta organizado en capas para separar responsabilidades y hacer el codigo mas legible:
- la CLI decide que ejecutar
- `csv_io` convierte archivos en estructuras de datos
- el core compartido implementa la lógica del algoritmo
- los backends serial y OpenMP implementan la fase de asignación/acumulación
- los scripts automatizan experimentos y visualización

## Vista arquitectónica

```mermaid
flowchart TB
    MAIN[src/main.c] --> CSV[src/csv_io.c]
    MAIN --> KRUN[src/kmeans_common.c]
    MAIN --> KS[src/kmeans_serial.c]
    MAIN --> KO[src/kmeans_omp.c]
    KRUN --> KINT[include/kmeans_core.h]
    KRUN --> RNG[include/rng.h]
    MAIN --> TIME[src/time_utils.c]
    RUN[scripts/run_experiments.ps1 /.sh] --> MAIN
```

### 1. Interfaz y orquestación
`src/main.c` no implementa detalles del algoritmo. Su trabajo es:
- parsear argumentos
- cargar el dataset
- elegir el backend
- medir tiempos
- escribir outputs opcionales
- registrar resultados experimentales
### 2. Core del algoritmo
`src/kmeans_common.c` contiene lo que es común a serial y OpenMP:
- validación del problema
- inicialización reproducible de centroides
- actualización de centroides
- loop principal de K-means
- asignación escalar reutilizable
- reserva/liberación de acumuladores
### 3. Backends
Los backends implementan solo la fase que realmente cambia:
- [[07_Modulos_y_Codigo#srckmeans_serialc]]: asignación y acumulación secuencial
- [[07_Modulos_y_Codigo#srckmeans_ompc]]: asignación paralela con acumuladores por hilo
### 4. I/O
`src/csv_io.c` encapsula:
- lectura de CSV con header opcional
- validacion de 2D o 3D
- escritura de assignments
- escritura de centroides
## Diagrama de dependencias

```mermaid
flowchart LR
    MAIN[src/main.c]
    CSV[src/csv_io.c]
    TIME[src/time_utils.c]
    CORE[src/kmeans_common.c]
    SERIAL[src/kmeans_serial.c]
    OMP[src/kmeans_omp.c]
    RNG[include/rng.h]


    MAIN --> CSV
    MAIN --> CORE
	MAIN --> TIME
    MAIN --> SERIAL
    MAIN --> OMP
    CORE --> RNG
    SERIAL --> CORE
    OMP --> CORE
```

## Flujo de alto nivel

```mermaid
sequenceDiagram
    participant U as Usuario
    participant M as main.c
    participant C as csv_io.c
    participant K as kmeans_common.c
    participant B as serial/omp backend

    U->>M: Ejecuta bin/kmeans con argumentos
    M->>C: Lee dataset CSV
    C-->>M: km_dataset_t
    M->>B: Selecciona backend segun --mode
    B->>K: Usa loop compartido
    K-->>M: assignments, centroides, stats
    M->>C: Escribe CSV de salida si se pide
    M-->>U: Imprime resumen y tiempos
```
