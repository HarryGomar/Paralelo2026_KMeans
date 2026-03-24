#include "kmeans.h"

#include <stdlib.h>
#include <string.h>

double km_update_centroids(const km_dataset_t *ds, int k, uint64_t *rng_state,
                           const double *sums, const int *counts, double *centroids);
int km_assign_serial(const km_dataset_t *ds, int k, const double *centroids, int *assignments,
                     double *sums, int *counts);

int km_run_serial(const km_dataset_t *ds, const km_params_t *params, int *assignments,
                  double *centroids, km_stats_t *stats) {
  if (!ds || !ds->points || !params || !assignments || !centroids) return 1;
  if (ds->dim != 2 && ds->dim != 3) return 1;
  if (params->k <= 0 || params->max_iters <= 0 || params->tol < 0.0) return 1;
  if ((size_t)params->k > ds->n) return 1;

  const int dim = ds->dim;
  const int k = params->k;

  uint64_t rng_state = params->seed;
  if (km_init_centroids(ds, k, &rng_state, centroids) != 0) return 1;

  double *sums = (double *)malloc((size_t)k * (size_t)dim * sizeof(double));
  int *counts = (int *)malloc((size_t)k * sizeof(int));
  if (!sums || !counts) {
    free(sums);
    free(counts);
    return 1;
  }

  int it = 0;
  int changed = 0;
  double max_shift = 0.0;

  for (it = 0; it < params->max_iters; it++) {
    changed = km_assign_serial(ds, k, centroids, assignments, sums, counts);
    max_shift = km_update_centroids(ds, k, &rng_state, sums, counts, centroids);
    if (changed == 0 || max_shift < params->tol) {
      it++;
      break;
    }
  }

  if (stats) {
    stats->iters = it;
    stats->changed_last_iter = changed;
    stats->max_centroid_shift = max_shift;
  }

  free(sums);
  free(counts);
  return 0;
}

