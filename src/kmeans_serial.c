#include "kmeans_core.h"

static int km_assign_points_serial(const km_dataset_t *ds, int k, const double *centroids,
                                   int *assignments, km_accum_t *accum, void *ctx) {
  (void)ctx;
  return km_assign_points_scalar(ds, k, centroids, assignments, accum);
}

int km_run_serial(const km_dataset_t *ds, const km_params_t *params, int *assignments,
                  double *centroids, km_stats_t *stats) {
  if (km_validate_problem(ds, params) != 0) return 1;
  if (!assignments || !centroids) return 1;

  km_accum_t accum;
  accum.sums = NULL;
  accum.counts = NULL;
  if (km_accum_init(&accum, params->k, ds->dim) != 0) return 1;

  const int rc = km_run_kmeans(ds, params, assignments, centroids, stats, &accum,
                               km_assign_points_serial, NULL);

  km_accum_free(&accum);
  return rc;
}
