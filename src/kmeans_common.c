#include "kmeans.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "rng.h"

static double dist2(const double *p, const double *c, int dim) {
  double acc = 0.0;
  for (int d = 0; d < dim; d++) {
    const double diff = p[d] - c[d];
    acc += diff * diff;
  }
  return acc;
}

int km_init_centroids(const km_dataset_t *ds, int k, uint64_t *rng_state, double *centroids) {
  if (!ds || !ds->points || !rng_state || !centroids) return 1;
  if (ds->dim != 2 && ds->dim != 3) return 1;
  if (k <= 0) return 1;
  if ((size_t)k > ds->n) return 1;

  const int dim = ds->dim;
  // Sample k points (allow repeats only if n==k? we avoid repeats deterministically).
  // We use a partial Fisher-Yates shuffle over indices 0..n-1 for the first k picks.
  size_t n = ds->n;
  size_t *idx = (size_t *)malloc(n * sizeof(size_t));
  if (!idx) return 1;
  for (size_t i = 0; i < n; i++) idx[i] = i;
  for (int i = 0; i < k; i++) {
    size_t r = (size_t)(rng_next_u64(rng_state) % (uint64_t)(n - (size_t)i));
    size_t pick_pos = (size_t)i + r;
    size_t pick_idx = idx[pick_pos];
    // swap idx[i] and idx[pick_pos]
    idx[pick_pos] = idx[(size_t)i];
    idx[(size_t)i] = pick_idx;

    memcpy(&centroids[(size_t)i * (size_t)dim], &ds->points[pick_idx * (size_t)dim],
           (size_t)dim * sizeof(double));
  }
  free(idx);
  return 0;
}

static void reinit_empty_cluster(const km_dataset_t *ds, uint64_t *rng_state, double *centroid) {
  size_t idx = (size_t)(rng_next_u64(rng_state) % (uint64_t)ds->n);
  memcpy(centroid, &ds->points[idx * (size_t)ds->dim], (size_t)ds->dim * sizeof(double));
}

double km_update_centroids(const km_dataset_t *ds, int k, uint64_t *rng_state,
                           const double *sums, const int *counts, double *centroids) {
  const int dim = ds->dim;
  double max_shift = 0.0;

  for (int c = 0; c < k; c++) {
    const int cnt = counts[c];
    double newc[3] = {0.0, 0.0, 0.0};

    if (cnt <= 0) {
      reinit_empty_cluster(ds, rng_state, newc);
    } else {
      for (int d = 0; d < dim; d++) {
        newc[d] = sums[(size_t)c * (size_t)dim + (size_t)d] / (double)cnt;
      }
    }

    double shift2 = 0.0;
    for (int d = 0; d < dim; d++) {
      const double diff = newc[d] - centroids[(size_t)c * (size_t)dim + (size_t)d];
      shift2 += diff * diff;
      centroids[(size_t)c * (size_t)dim + (size_t)d] = newc[d];
    }
    const double shift = sqrt(shift2);
    if (shift > max_shift) max_shift = shift;
  }
  return max_shift;
}

int km_assign_serial(const km_dataset_t *ds, int k, const double *centroids, int *assignments,
                     double *sums, int *counts) {
  const int dim = ds->dim;
  memset(sums, 0, (size_t)k * (size_t)dim * sizeof(double));
  memset(counts, 0, (size_t)k * sizeof(int));

  int changed = 0;
  for (size_t i = 0; i < ds->n; i++) {
    const double *p = &ds->points[i * (size_t)dim];

    int best = 0;
    double best_d2 = dist2(p, &centroids[0], dim);
    for (int c = 1; c < k; c++) {
      double d2 = dist2(p, &centroids[(size_t)c * (size_t)dim], dim);
      if (d2 < best_d2) {
        best_d2 = d2;
        best = c;
      }
    }

    if (assignments[i] != best) changed++;
    assignments[i] = best;
    counts[best]++;
    for (int d = 0; d < dim; d++) {
      sums[(size_t)best * (size_t)dim + (size_t)d] += p[d];
    }
  }
  return changed;
}

// Expose helpers to the translation units.
double km_update_centroids(const km_dataset_t *ds, int k, uint64_t *rng_state,
                           const double *sums, const int *counts, double *centroids);
int km_assign_serial(const km_dataset_t *ds, int k, const double *centroids, int *assignments,
                     double *sums, int *counts);
