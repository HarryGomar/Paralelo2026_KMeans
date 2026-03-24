#include "kmeans.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

double km_update_centroids(const km_dataset_t *ds, int k, uint64_t *rng_state,
                           const double *sums, const int *counts, double *centroids);

#ifdef _OPENMP
#include <omp.h>
#endif

static size_t roundup_size(size_t x, size_t multiple) {
  if (multiple == 0) return x;
  size_t rem = x % multiple;
  if (rem == 0) return x;
  return x + (multiple - rem);
}

static void *aligned_calloc64(size_t nbytes) {
  // C11 aligned_alloc requires size to be a multiple of alignment.
  const size_t sz = roundup_size(nbytes, 64);
  void *p = aligned_alloc(64, sz);
  if (!p) return NULL;
  memset(p, 0, sz);
  return p;
}

static inline double dist2_inline(const double *p, const double *c, int dim) {
  double acc = 0.0;
  for (int d = 0; d < dim; d++) {
    const double diff = p[d] - c[d];
    acc += diff * diff;
  }
  return acc;
}

int km_run_omp(const km_dataset_t *ds, const km_params_t *params, int threads,
               int *assignments, double *centroids, km_stats_t *stats) {
  if (!ds || !ds->points || !params || !assignments || !centroids) return 1;
  if (ds->dim != 2 && ds->dim != 3) return 1;
  if (params->k <= 0 || params->max_iters <= 0 || params->tol < 0.0) return 1;
  if ((size_t)params->k > ds->n) return 1;
  if (threads <= 0) threads = 1;

  const int dim = ds->dim;
  const int k = params->k;

  uint64_t rng_state = params->seed;
  if (km_init_centroids(ds, k, &rng_state, centroids) != 0) return 1;

  int max_threads = threads;
#ifdef _OPENMP
  if (max_threads > omp_get_max_threads()) max_threads = omp_get_max_threads();
#else
  max_threads = 1;
#endif

  // IMPORTANT: pad per-thread blocks to 64B boundaries to avoid false sharing.
  // - sums stride in doubles: multiple of 8 doubles (64 bytes)
  // - counts stride in ints: multiple of 16 ints (64 bytes)
  const size_t sums_block = (size_t)k * (size_t)dim;
  const size_t sums_stride = roundup_size(sums_block, 8);
  const size_t counts_block = (size_t)k;
  const size_t counts_stride = roundup_size(counts_block, 16);

  double *thread_sums =
      (double *)aligned_calloc64((size_t)max_threads * sums_stride * sizeof(double));
  int *thread_counts = (int *)aligned_calloc64((size_t)max_threads * counts_stride * sizeof(int));
  if (!thread_sums || !thread_counts) {
    free(thread_sums);
    free(thread_counts);
    return 1;
  }

  double *global_sums = (double *)malloc((size_t)k * (size_t)dim * sizeof(double));
  int *global_counts = (int *)malloc((size_t)k * sizeof(int));
  if (!global_sums || !global_counts) {
    free(thread_sums);
    free(thread_counts);
    free(global_sums);
    free(global_counts);
    return 1;
  }

  int it = 0;
  int changed = 0;
  double max_shift = 0.0;

  for (it = 0; it < params->max_iters; it++) {
    memset(thread_sums, 0, (size_t)max_threads * sums_stride * sizeof(double));
    memset(thread_counts, 0, (size_t)max_threads * counts_stride * sizeof(int));

#ifdef _OPENMP
#pragma omp parallel num_threads(max_threads) reduction(+ : changed)
    {
      const int tid = omp_get_thread_num();
      double *local_sums = &thread_sums[(size_t)tid * sums_stride];
      int *local_counts = &thread_counts[(size_t)tid * counts_stride];
      int local_changed = 0;

#pragma omp for schedule(static)
      for (size_t i = 0; i < ds->n; i++) {
        const double *p = &ds->points[i * (size_t)dim];

        int best = 0;
        double best_d2 = dist2_inline(p, &centroids[0], dim);
        for (int c = 1; c < k; c++) {
          double d2 = dist2_inline(p, &centroids[(size_t)c * (size_t)dim], dim);
          if (d2 < best_d2) {
            best_d2 = d2;
            best = c;
          }
        }

        if (assignments[i] != best) local_changed++;
        assignments[i] = best;
        local_counts[best]++;
        for (int d = 0; d < dim; d++) {
          local_sums[(size_t)best * (size_t)dim + (size_t)d] += p[d];
        }
      }
      changed += local_changed;
    }
#else
    // Fallback to serial behavior if not compiled with OpenMP.
    changed = 0;
    for (size_t i = 0; i < ds->n; i++) {
      const double *p = &ds->points[i * (size_t)dim];

      int best = 0;
      double best_d2 = dist2_inline(p, &centroids[0], dim);
      for (int c = 1; c < k; c++) {
        double d2 = dist2_inline(p, &centroids[(size_t)c * (size_t)dim], dim);
        if (d2 < best_d2) {
          best_d2 = d2;
          best = c;
        }
      }

      if (assignments[i] != best) changed++;
      assignments[i] = best;
      thread_counts[best]++;
      for (int d = 0; d < dim; d++) {
        thread_sums[(size_t)best * (size_t)dim + (size_t)d] += p[d];
      }
    }
#endif

    // Manual reduction: thread-local accumulators -> global.
    memset(global_sums, 0, (size_t)k * (size_t)dim * sizeof(double));
    memset(global_counts, 0, (size_t)k * sizeof(int));

    for (int c = 0; c < k; c++) {
      int cnt = 0;
      for (int t = 0; t < max_threads; t++) {
        cnt += thread_counts[(size_t)t * counts_stride + (size_t)c];
      }
      global_counts[c] = cnt;

      for (int d = 0; d < dim; d++) {
        double s = 0.0;
        const size_t off = (size_t)c * (size_t)dim + (size_t)d;
        for (int t = 0; t < max_threads; t++) {
          s += thread_sums[(size_t)t * sums_stride + off];
        }
        global_sums[(size_t)c * (size_t)dim + (size_t)d] = s;
      }
    }

    max_shift = km_update_centroids(ds, k, &rng_state, global_sums, global_counts, centroids);
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

  free(thread_sums);
  free(thread_counts);
  free(global_sums);
  free(global_counts);
  return 0;
}
