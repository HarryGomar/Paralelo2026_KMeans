#pragma once

#include <stdint.h>

#include "csv_io.h"

typedef enum {
  KM_MODE_SERIAL = 0,
  KM_MODE_OMP = 1,
} km_mode_t;

typedef struct {
  int k;
  int max_iters;
  double tol;
  uint64_t seed;
} km_params_t;

typedef struct {
  int iters;
  int changed_last_iter;
  double max_centroid_shift;
} km_stats_t;

// Fills `centroids` (length k*dim) using random points from the dataset.
// rng_state is updated. Returns 0 on success.
int km_init_centroids(const km_dataset_t *ds, int k, uint64_t *rng_state, double *centroids);

// Runs classic K-means (assignment + update) until convergence or max_iters.
// `assignments` length ds->n, allocated by caller.
// `centroids` length k*dim, output final centroids.
int km_run_serial(const km_dataset_t *ds, const km_params_t *params, int *assignments,
                  double *centroids, km_stats_t *stats);

// Runs the OpenMP backend using the exact number of threads requested by `threads`.
int km_run_omp(const km_dataset_t *ds, const km_params_t *params, int threads,
               int *assignments, double *centroids, km_stats_t *stats);
