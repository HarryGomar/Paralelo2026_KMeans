# Reporte de Proyecto: K-means Paralelo con OpenMP

**Materia:** [Nombre de la materia]  
**Profesor(a):** [Nombre del profesor o profesora]  
**Integrantes:** [Nombre 1] - [Matricula 1], [Nombre 2] - [Matricula 2]  
**Grupo:** [Grupo]  
**Fecha:** 24 de marzo de 2026

## 1. Objetivo

El objetivo del proyecto fue implementar el algoritmo de agrupamiento K-means en lenguaje C en dos
versiones:

- una version serial
- una version paralela con OpenMP

Ademas de implementar ambas versiones, el proyecto exigio evaluar experimentalmente el rendimiento
de la version paralela respecto a la serial, medir speedup y documentar el comportamiento observado
en diferentes tamanos de entrada, dimensiones y cantidades de hilos.

## 2. Descripcion general del codigo

El proyecto quedo organizado para separar la logica del algoritmo, la interfaz de linea de comandos
y la lectura/escritura de archivos.

### 2.1 Organizacion del proyecto

- `src/main.c`: procesa argumentos, carga el CSV de entrada, ejecuta K-means y escribe resultados.
- `src/kmeans_common.c`: contiene el flujo comun del algoritmo, la inicializacion de centroides y
  la actualizacion de centroides.
- `src/kmeans_serial.c`: implementa la fase de asignacion/acumulacion en modo serial.
- `src/kmeans_omp.c`: implementa la fase de asignacion/acumulacion en modo paralelo con OpenMP.
- `src/csv_io.c`: lectura de datasets CSV y escritura de archivos de salida.
- `scripts/run_experiments.sh`: ejecuta el grid experimental completo y valida el CSV final.
- `scripts/plot_speedup.py`: calcula speedup a partir de `results/experiments.csv` y genera
  graficas.

### 2.2 Flujo del algoritmo

El algoritmo implementado corresponde a K-means clasico:

1. Se leen los puntos desde un archivo CSV de 2D o 3D.
2. Se seleccionan `k` centroides iniciales de forma aleatoria pero reproducible usando una semilla.
3. En cada iteracion:
   - cada punto se asigna al centroide mas cercano
   - se acumulan sumas y conteos por cluster
   - cada centroide se actualiza como el promedio de los puntos asignados
4. El algoritmo termina cuando ocurre alguna de estas condiciones:
   - ningun punto cambia de cluster
   - el desplazamiento maximo de centroides es menor que `tol`
   - se alcanza `max_iters`

### 2.3 Entrada y salida

**Entrada**

- Archivo CSV con 2 o 3 columnas numericas
- Encabezado opcional
- Datos sinteticos generados con `scripts/generate_synthetic.py`

**Salida**

- CSV con puntos etiquetados: `x,y,cluster` o `x,y,z,cluster`
- CSV con centroides finales: `cluster,cx,cy` o `cluster,cx,cy,cz`
- CSV de resultados experimentales
- CSV de speedup y graficas PNG

## 3. Estrategia de paralelizacion

La parte mas costosa del algoritmo es la asignacion de cada punto al centroide mas cercano. Esta
fase es dominante porque, en cada iteracion, se recorren todos los puntos y se compara cada uno con
los `k` centroides.

### 3.1 Decision de paralelizacion

Se paralelizo el recorrido de los puntos usando OpenMP. La idea central fue dividir el conjunto de
puntos entre los hilos y permitir que cada hilo procese de manera independiente su bloque de datos.

### 3.2 Problema principal: condiciones de carrera

Durante la asignacion no solo se determina el cluster de cada punto, tambien se acumulan:

- la suma de coordenadas de cada cluster
- el numero de puntos asignados a cada cluster

Si varios hilos actualizaran directamente estas estructuras globales, habria condiciones de carrera
y una gran contencion.

### 3.3 Solucion implementada

Para evitar ese problema, se usaron acumuladores privados por hilo:

- `local_sums[thread][cluster][dim]`
- `local_counts[thread][cluster]`

Cada hilo actualiza solo sus propios acumuladores. Al terminar la region paralela, se hace una
reduccion manual para combinar todos los acumuladores locales en arreglos globales. Despues, la fase
de actualizacion de centroides se ejecuta una sola vez sobre esos acumuladores globales.

### 3.4 Ventajas de esta estrategia

- evita el uso de `critical` o `atomic` por cada punto
- reduce la contencion entre hilos
- mantiene una estructura clara y facil de razonar
- permite mejorar el rendimiento especialmente cuando el numero de puntos es grande

### 3.5 Detalles de implementacion relevantes

- Se usa `schedule(static)` para repartir el trabajo de forma simple y con bajo overhead.
- Los buffers por hilo se reservan una sola vez por corrida.
- La reduccion se hace en una fase separada y bien delimitada.
- La logica del algoritmo se comparte entre modo serial y paralelo, por lo que ambas versiones
  conservan exactamente la misma semantica de convergencia.

## 4. Definicion del experimento de rendimiento

La evaluacion se definio siguiendo los parametros solicitados en la descripcion del proyecto.

### 4.1 Variables del experimento

- Dimension: `2` y `3`
- Numero de puntos:
  `100000, 200000, 300000, 400000, 600000, 800000, 1000000`
- Numero de hilos:
  `1`, `vcores/2`, `vcores`, `2*vcores`
- Numero de repeticiones por configuracion: `10`
- Numero de clusters: `k = 5`

En el equipo usado para las pruebas se detectaron `12` cores virtuales, por lo que la malla de
hilos correspondiente es:

- `1`
- `6`
- `12`
- `24`

### 4.2 Datos de entrada

Los datos de entrada son puntos sinteticos generados aleatoriamente en 2D y 3D. El generador crea
centros aleatorios y distribuye los puntos alrededor de esos centros usando ruido gaussiano.

Esto permite:

- controlar el numero de puntos
- generar datasets comparables entre corridas
- mantener reproducibilidad usando semillas

### 4.3 Metricas medidas

Se registran dos tiempos:

- `kernel_ms`: tiempo del nucleo de K-means sin I/O
- `total_ms`: tiempo total incluyendo lectura del CSV y escritura de resultados cuando aplica

La metrica principal para comparar rendimiento y calcular speedup es `kernel_ms`, ya que aisla el
costo de computo paralelo.

### 4.4 Formula de speedup

Para cada configuracion:

```text
speedup = promedio(kernel_ms_serial) / promedio(kernel_ms_omp)
```

Esta definicion permite comparar directamente la aceleracion lograda por la version paralela contra
la version serial.

### 4.5 Automatizacion del experimento

El script `scripts/run_experiments.sh`:

- compila el proyecto
- genera datasets sinteticos si no existen
- ejecuta las 10 corridas por configuracion
- guarda los resultados en `results/experiments.csv`
- captura informacion del sistema en `results/system_info.txt`
- valida que el CSV final tenga el numero correcto de filas

El script `scripts/plot_speedup.py`:

- agrupa tiempos promedio por configuracion
- calcula el speedup
- genera `results/speedup.csv`
- genera una grafica PNG por dimension

## 5. Equipo de pruebas

La informacion del equipo se obtuvo de `results/system_info.txt`.

### 5.1 Hardware

- CPU: Intel Core i5-13420H de 13a generacion
- Cores virtuales detectados: `12`
- Hilos por nucleo: `2`
- Socket: `1`
- Cache L3: `12 MiB`

### 5.2 Software

- Sistema operativo: Ubuntu 24.04 sobre Linux 6.17.0-19-generic
- Arquitectura: `x86_64`
- Compilador: GCC 13.3.0

Estas caracteristicas son relevantes porque el rendimiento paralelo depende fuertemente del numero
de hilos disponibles, la jerarquia de memoria y el costo de sincronizacion.

## 6. Resultados

Los resultados resumidos se encuentran en:

- `results/speedup.csv`
- `results/speedup_dim2.png`
- `results/speedup_dim3.png`

### 6.1 Graficas

**Speedup en 2D**

![Speedup 2D](../results/speedup_dim2.png)

**Speedup en 3D**

![Speedup 3D](../results/speedup_dim3.png)

### 6.2 Resumen numerico

A partir de `results/speedup.csv` se observaron los siguientes valores destacados:

- Mejor speedup global: `3.409606`
- Configuracion del mejor speedup global: `dim=2`, `N=1000000`, `k=5`, `threads=12`
- Mejor speedup en 3D: `3.155003`
- Configuracion del mejor speedup en 3D: `dim=3`, `N=800000`, `k=5`, `threads=12`

Promedios por numero de hilos observados en el resumen:

- 2D con `1` hilo: speedup promedio `0.9680`
- 2D con `6` hilos: speedup promedio `2.2977`
- 2D con `12` hilos: speedup promedio `2.4731`
- 3D con `1` hilo: speedup promedio `0.9954`
- 3D con `6` hilos: speedup promedio `2.2945`
- 3D con `12` hilos: speedup promedio `2.4880`

## 7. Interpretacion y analisis de resultados

### 7.1 Cumplimiento del requisito de speedup

La nota del proyecto exige que la version paralela sea mas rapida que la serial y que al menos en
alguna configuracion se observe un speedup de `1.5` o mayor.

Ese requisito se cumple con margen amplio. El mejor caso medido fue `3.409606`, por lo que la
implementacion satisface la condicion solicitada.

### 7.2 Comportamiento con 1 hilo

En varias configuraciones, ejecutar la version OpenMP con `1` hilo no supera a la version serial.
Esto es esperable, porque aun usando un solo hilo la version paralela conserva cierto overhead:

- preparacion del entorno OpenMP
- buffers adicionales
- reduccion de acumuladores

Por eso los speedups con `1` hilo oscilan alrededor de `1` e incluso pueden quedar ligeramente por
debajo.

### 7.3 Comportamiento con 6 y 12 hilos

Con `6` y `12` hilos aparece una mejora clara. Esto indica que el trabajo por punto es lo
suficientemente grande para amortizar el overhead de paralelizacion. En general:

- al aumentar `N`, el speedup tiende a mejorar
- el beneficio de OpenMP se vuelve mas claro en entradas grandes
- `12` hilos suele superar a `6` hilos, aunque no de forma lineal

La falta de linealidad perfecta es normal y se debe a:

- ancho de banda de memoria
- efectos de cache
- costo de reduccion
- sobrecarga del runtime

### 7.4 Diferencias entre 2D y 3D

En 3D cada distancia involucra una coordenada adicional, lo que incrementa el trabajo computacional
por punto. En consecuencia, la relacion entre trabajo util y overhead mejora y eso favorece la
paralelizacion. Aun asi, los resultados de 2D y 3D son similares en promedio para `6` y `12`
hilos, lo cual muestra que la implementacion se comporta de manera estable en ambas dimensiones.

### 7.5 Efecto del tamano de entrada

Los mejores speedups aparecen cuando `N` es grande. Esto confirma un principio basico de computo
paralelo: cuando el volumen de trabajo aumenta, el costo fijo de administrar hilos representa una
fraccion menor del tiempo total, por lo que la aceleracion tiende a ser mejor.

### 7.6 Sobre-suscripcion

La malla experimental contempla tambien `2*vcores`. En el refactor final del codigo, la opcion
`--threads` se respeta tal como se solicita, de modo que el experimento puede ejecutarse realmente
con sobre-suscripcion cuando se quiera evaluar ese caso. Esta medicion es util para comprobar si
usar mas hilos que cores virtuales aporta alguna ventaja o solo agrega overhead.

## 8. Conclusiones

Se implementaron correctamente:

- una version serial de K-means
- una version paralela con OpenMP
- un pipeline de experimentacion reproducible
- un conjunto de scripts para generar speedup y graficas

La version paralela logra una mejora clara respecto a la serial en entradas medianas y grandes. El
mejor resultado observado fue un speedup de `3.409606`, superior al minimo requerido por la
especificacion del proyecto.

Desde el punto de vista de ingenieria, el refactor final del codigo dejo una estructura mas limpia y
facil de mantener:

- el flujo del algoritmo esta centralizado en un nucleo comun
- serial y OpenMP solo difieren en la fase de asignacion/acumulacion
- la CLI, el I/O y el benchmarking quedaron desacoplados del algoritmo

En conjunto, el proyecto cumple el objetivo academico de implementar, paralelizar y evaluar K-means
de forma clara, reproducible y documentada.

## 9. Reproduccion

Compilar:

```bash
make
```

Ejemplo serial:

```bash
./bin/kmeans --input data/synth_2d_N100000.csv --k 5 --mode serial --out results/out.csv \
  --centroids results/centroids.csv
```

Ejemplo OpenMP:

```bash
./bin/kmeans --input data/synth_2d_N100000.csv --k 5 --mode omp --threads 12 \
  --out results/out.csv --centroids results/centroids.csv
```

Correr experimento:

```bash
./scripts/run_experiments.sh
```

Generar speedup y graficas:

```bash
python3 scripts/plot_speedup.py --input results/experiments.csv --outdir results
```
