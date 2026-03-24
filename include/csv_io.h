#pragma once

#include <stddef.h>

typedef struct {
  int dim;          // 2 o 3
  size_t n;         // cantidad de puntos
  double *points;   // buffer lineal de largo n*dim en row-major
} km_dataset_t;

// Lee un CSV de 2D o 3D.
// `dim_hint=0` infiere la dimension; si no, la valida.
// Tolera un header opcional antes de la primera fila numerica.
int csv_read_points(const char *path, int dim_hint, km_dataset_t *out, char *errbuf,
                    size_t errbuf_sz);

void km_dataset_free(km_dataset_t *ds);

// Escribe `x,y[,z],cluster`. `NULL` no hace nada, `"-"` usa stdout.
int csv_write_assignments(const char *path, const km_dataset_t *ds, const int *assignments,
                          char *errbuf, size_t errbuf_sz);

// Escribe `cluster,cx,cy[,cz]`. `NULL` no hace nada, `"-"` usa stdout.
int csv_write_centroids(const char *path, int dim, int k, const double *centroids,
                        char *errbuf, size_t errbuf_sz);

