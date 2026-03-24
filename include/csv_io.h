#pragma once

#include <stddef.h>

typedef struct {
  int dim;          // 2 or 3
  size_t n;         // number of points
  double *points;   // length n*dim, row-major
} km_dataset_t;

// Reads a CSV with 2 or 3 numeric columns (comma-separated).
// - dim_hint: 0 to infer; otherwise must be 2 or 3.
// - Handles an optional header line (first non-numeric line before any data).
// Returns 0 on success, non-zero on error (details in errbuf).
int csv_read_points(const char *path, int dim_hint, km_dataset_t *out, char *errbuf,
                    size_t errbuf_sz);

void km_dataset_free(km_dataset_t *ds);

// Writes per-point output CSV: x,y[,z],cluster
// If path is NULL, does nothing and returns 0.
// If path is "-", writes to stdout.
int csv_write_assignments(const char *path, const km_dataset_t *ds, const int *assignments,
                          char *errbuf, size_t errbuf_sz);

// Writes centroid CSV: cluster,cx,cy[,cz]
// If path is NULL, does nothing and returns 0.
// If path is "-", writes to stdout.
int csv_write_centroids(const char *path, int dim, int k, const double *centroids,
                        char *errbuf, size_t errbuf_sz);

