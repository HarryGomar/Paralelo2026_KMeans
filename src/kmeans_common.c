#include "kmeans_core.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "rng.h"

int km_validate_problem(const km_dataset_t *ds, const km_params_t *params) {
  if (!ds || !ds->points || !params) return 1;
  if (ds->dim != 2 && ds->dim != 3) return 1;
  if (params->k <= 0 || params->max_iters <= 0 || params->tol < 0.0) return 1;
  if ((size_t)params->k > ds->n) return 1;
  return 0;
}

int km_accum_init(km_accum_t *accum, int k, int dim) {
  if (!accum || k <= 0 || (dim != 2 && dim != 3)) return 1;

  accum->sums = (double *)malloc((size_t)k * (size_t)dim * sizeof(*accum->sums));
  accum->counts = (int *)malloc((size_t)k * sizeof(*accum->counts));
  if (!accum->sums || !accum->counts) {
    km_accum_free(accum);
    return 1;
  }

  return 0;
}

void km_accum_free(km_accum_t *accum) {
  if (!accum) return;
  free(accum->sums);
  free(accum->counts);
  accum->sums = NULL;
  accum->counts = NULL;
}

int km_init_centroids(const km_dataset_t *ds, int k, uint64_t *rng_state, double *centroids) {
  if (!ds || !ds->points || !rng_state || !centroids) return 1;
  if (ds->dim != 2 && ds->dim != 3) return 1;
  if (k <= 0 || (size_t)k > ds->n) return 1;

  size_t *indices = (size_t *)malloc(ds->n * sizeof(*indices));
  if (!indices) return 1;

  for (size_t i = 0; i < ds->n; i++) indices[i] = i;

  for (int c = 0; c < k; c++) {
    const size_t remaining = ds->n - (size_t)c;
    const size_t offset = (size_t)(rng_next_u64(rng_state) % (uint64_t)remaining);
    const size_t pick_pos = (size_t)c + offset;
    const size_t pick_idx = indices[pick_pos];

    indices[pick_pos] = indices[(size_t)c];
    indices[(size_t)c] = pick_idx;

    memcpy(&centroids[(size_t)c * (size_t)ds->dim], &ds->points[pick_idx * (size_t)ds->dim],
           (size_t)ds->dim * sizeof(*centroids));
  }

  free(indices);
  return 0;
}

static void km_reinit_empty_cluster(const km_dataset_t *ds, uint64_t *rng_state, double *centroid) {
  const size_t idx = (size_t)(rng_next_u64(rng_state) % (uint64_t)ds->n);
  memcpy(centroid, &ds->points[idx * (size_t)ds->dim], (size_t)ds->dim * sizeof(*centroid));
}

double km_update_centroids(const km_dataset_t *ds, int k, uint64_t *rng_state,
                           const double *sums, const int *counts, double *centroids) {
  double max_shift = 0.0;

  for (int c = 0; c < k; c++) {
    double next[3] = {0.0, 0.0, 0.0};
    if (counts[c] <= 0) {
      km_reinit_empty_cluster(ds, rng_state, next);
    } else {
      for (int d = 0; d < ds->dim; d++) {
        next[d] = sums[(size_t)c * (size_t)ds->dim + (size_t)d] / (double)counts[c];
      }
    }

    double shift2 = 0.0;
    for (int d = 0; d < ds->dim; d++) {
      const size_t offset = (size_t)c * (size_t)ds->dim + (size_t)d;
      const double diff = next[d] - centroids[offset];
      shift2 += diff * diff;
      centroids[offset] = next[d];
    }

    const double shift = sqrt(shift2);
    if (shift > max_shift) max_shift = shift;
  }

  return max_shift;
}

int km_assign_points_scalar(const km_dataset_t *ds, int k, const double *centroids,
                            int *assignments, km_accum_t *accum) {
  memset(accum->sums, 0, (size_t)k * (size_t)ds->dim * sizeof(*accum->sums));
  memset(accum->counts, 0, (size_t)k * sizeof(*accum->counts));

  int changed = 0;
  for (size_t i = 0; i < ds->n; i++) {
    const double *point = &ds->points[i * (size_t)ds->dim];
    const int best = km_find_nearest_centroid(point, centroids, k, ds->dim);

    if (assignments[i] != best) changed++;
    assignments[i] = best;
    accum->counts[best]++;

    for (int d = 0; d < ds->dim; d++) {
      accum->sums[(size_t)best * (size_t)ds->dim + (size_t)d] += point[d];
    }
  }

  return changed;
}

int km_run_kmeans(const km_dataset_t *ds, const km_params_t *params, int *assignments,
                  double *centroids, km_stats_t *stats, km_accum_t *accum,
                  km_assign_fn assign_points, void *assign_ctx) {
  if (km_validate_problem(ds, params) != 0) return 1;
  if (!assignments || !centroids || !accum || !accum->sums || !accum->counts || !assign_points) {
    return 1;
  }

  for (size_t i = 0; i < ds->n; i++) assignments[i] = -1;

  uint64_t rng_state = params->seed;
  if (km_init_centroids(ds, params->k, &rng_state, centroids) != 0) return 1;

  int iters = 0;
  int changed = 0;
  double max_shift = 0.0;

  for (iters = 0; iters < params->max_iters; iters++) {
    changed = assign_points(ds, params->k, centroids, assignments, accum, assign_ctx);
    if (changed < 0) return 1;

    max_shift = km_update_centroids(ds, params->k, &rng_state, accum->sums, accum->counts,
                                    centroids);
    if (changed == 0 || max_shift < params->tol) {
      iters++;
      break;
    }
  }

  if (stats) {
    stats->iters = iters;
    stats->changed_last_iter = changed;
    stats->max_centroid_shift = max_shift;
  }

  return 0;
}
