#include "kmeans_core.h"

#include <stdlib.h>
#include <string.h>

#ifdef _OPENMP
#include <omp.h>
#endif

typedef struct {
  int threads;
  size_t sums_stride;
  size_t counts_stride;
  double *thread_sums;
  int *thread_counts;
} km_omp_ctx_t;

static size_t km_round_up(size_t value, size_t multiple) {
  if (multiple == 0) return value;
  const size_t rem = value % multiple;
  return (rem == 0) ? value : (value + multiple - rem);
}

static void *km_aligned_calloc(size_t count, size_t elem_size, size_t alignment) {
  if (count == 0 || elem_size == 0) return NULL;
  const size_t bytes = count * elem_size;
  const size_t padded = km_round_up(bytes, alignment);
  void *ptr = aligned_alloc(alignment, padded);
  if (!ptr) return NULL;
  memset(ptr, 0, padded);
  return ptr;
}

static int km_assign_points_fallback(const km_dataset_t *ds, int k, const double *centroids,
                                     int *assignments, km_accum_t *accum) {
  return km_assign_points_scalar(ds, k, centroids, assignments, accum);
}

static int km_assign_points_omp(const km_dataset_t *ds, int k, const double *centroids,
                                int *assignments, km_accum_t *accum, void *ctx_ptr) {
  km_omp_ctx_t *ctx = (km_omp_ctx_t *)ctx_ptr;
  if (!ctx || !ctx->thread_sums || !ctx->thread_counts) return -1;

  memset(ctx->thread_sums, 0,
         (size_t)ctx->threads * ctx->sums_stride * sizeof(*ctx->thread_sums));
  memset(ctx->thread_counts, 0,
         (size_t)ctx->threads * ctx->counts_stride * sizeof(*ctx->thread_counts));

  int changed = 0;

#ifdef _OPENMP
#pragma omp parallel num_threads(ctx->threads) reduction(+ : changed)
  {
    const int tid = omp_get_thread_num();
    double *local_sums = &ctx->thread_sums[(size_t)tid * ctx->sums_stride];
    int *local_counts = &ctx->thread_counts[(size_t)tid * ctx->counts_stride];
    int local_changed = 0;

#pragma omp for schedule(static)
    for (size_t i = 0; i < ds->n; i++) {
      const double *point = &ds->points[i * (size_t)ds->dim];
      const int best = km_find_nearest_centroid(point, centroids, k, ds->dim);

      if (assignments[i] != best) local_changed++;
      assignments[i] = best;
      local_counts[best]++;

      for (int d = 0; d < ds->dim; d++) {
        local_sums[(size_t)best * (size_t)ds->dim + (size_t)d] += point[d];
      }
    }

    changed += local_changed;
  }
#else
  changed = km_assign_points_fallback(ds, k, centroids, assignments, accum);
  return changed;
#endif

  memset(accum->sums, 0, (size_t)k * (size_t)ds->dim * sizeof(*accum->sums));
  memset(accum->counts, 0, (size_t)k * sizeof(*accum->counts));

  for (int t = 0; t < ctx->threads; t++) {
    const double *local_sums = &ctx->thread_sums[(size_t)t * ctx->sums_stride];
    const int *local_counts = &ctx->thread_counts[(size_t)t * ctx->counts_stride];

    for (int c = 0; c < k; c++) {
      accum->counts[c] += local_counts[c];
      for (int d = 0; d < ds->dim; d++) {
        accum->sums[(size_t)c * (size_t)ds->dim + (size_t)d] +=
            local_sums[(size_t)c * (size_t)ds->dim + (size_t)d];
      }
    }
  }

  return changed;
}

int km_run_omp(const km_dataset_t *ds, const km_params_t *params, int threads,
               int *assignments, double *centroids, km_stats_t *stats) {
  if (km_validate_problem(ds, params) != 0) return 1;
  if (!assignments || !centroids || threads <= 0) return 1;

#ifdef _OPENMP
  omp_set_dynamic(0);
#else
  threads = 1;
#endif

  km_accum_t accum;
  accum.sums = NULL;
  accum.counts = NULL;
  if (km_accum_init(&accum, params->k, ds->dim) != 0) return 1;

  km_omp_ctx_t ctx;
  ctx.threads = threads;
  ctx.sums_stride = km_round_up((size_t)params->k * (size_t)ds->dim, 8);
  ctx.counts_stride = km_round_up((size_t)params->k, 16);
  ctx.thread_sums =
      (double *)km_aligned_calloc((size_t)threads * ctx.sums_stride, sizeof(*ctx.thread_sums), 64);
  ctx.thread_counts =
      (int *)km_aligned_calloc((size_t)threads * ctx.counts_stride, sizeof(*ctx.thread_counts), 64);
  if (!ctx.thread_sums || !ctx.thread_counts) {
    km_accum_free(&accum);
    free(ctx.thread_sums);
    free(ctx.thread_counts);
    return 1;
  }

  const int rc = km_run_kmeans(ds, params, assignments, centroids, stats, &accum,
                               km_assign_points_omp, &ctx);

  km_accum_free(&accum);
  free(ctx.thread_sums);
  free(ctx.thread_counts);
  return rc;
}
