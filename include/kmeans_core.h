#pragma once

#include "kmeans.h"

typedef struct {
  double *sums;
  int *counts;
} km_accum_t;

// Backend hook for the assignment+accumulation phase.
typedef int (*km_assign_fn)(const km_dataset_t *ds, int k, const double *centroids,
                            int *assignments, km_accum_t *accum, void *ctx);

static inline double km_dist2(const double *point, const double *centroid, int dim) {
  double acc = 0.0;
  for (int d = 0; d < dim; d++) {
    const double diff = point[d] - centroid[d];
    acc += diff * diff;
  }
  return acc;
}

static inline int km_find_nearest_centroid(const double *point, const double *centroids, int k,
                                           int dim) {
  int best = 0;
  double best_d2 = km_dist2(point, centroids, dim);
  for (int c = 1; c < k; c++) {
    const double *candidate = &centroids[(size_t)c * (size_t)dim];
    const double d2 = km_dist2(point, candidate, dim);
    if (d2 < best_d2) {
      best_d2 = d2;
      best = c;
    }
  }
  return best;
}

int km_validate_problem(const km_dataset_t *ds, const km_params_t *params);
int km_accum_init(km_accum_t *accum, int k, int dim);
void km_accum_free(km_accum_t *accum);
int km_init_centroids(const km_dataset_t *ds, int k, uint64_t *rng_state, double *centroids);
double km_update_centroids(const km_dataset_t *ds, int k, uint64_t *rng_state,
                           const double *sums, const int *counts, double *centroids);
int km_assign_points_scalar(const km_dataset_t *ds, int k, const double *centroids,
                            int *assignments, km_accum_t *accum);

// Shared K-means loop used by both serial and OpenMP backends.
int km_run_kmeans(const km_dataset_t *ds, const km_params_t *params, int *assignments,
                  double *centroids, km_stats_t *stats, km_accum_t *accum,
                  km_assign_fn assign_points, void *assign_ctx);
