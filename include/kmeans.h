#pragma once

#include <stdint.h>

#include "csv_io.h"

typedef enum {
  KM_MODE_SERIAL = 0,
  KM_MODE_OMP = 1,
} km_mode_t;

// Parametros visibles desde CLI y compartidos por ambos backends.
typedef struct {
  int k;
  int max_iters;
  double tol;
  uint64_t seed;
} km_params_t;

// Resumen de convergencia de la ultima corrida.
typedef struct {
  int iters;
  int changed_last_iter;
  double max_centroid_shift;
} km_stats_t;

// Inicializa `centroids` con k puntos distintos del dataset.
// `rng_state` avanza para mantener reproducibilidad.
int km_init_centroids(const km_dataset_t *ds, int k, uint64_t *rng_state, double *centroids);

// Ejecuta K-means clasico hasta converger o agotar `max_iters`.
// `assignments` y `centroids` son buffers provistos por el caller.
int km_run_serial(const km_dataset_t *ds, const km_params_t *params, int *assignments,
                  double *centroids, km_stats_t *stats);

// Ejecuta el backend OpenMP usando exactamente `threads` hilos.
int km_run_omp(const km_dataset_t *ds, const km_params_t *params, int threads,
               int *assignments, double *centroids, km_stats_t *stats);
