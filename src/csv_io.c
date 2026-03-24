#include "csv_io.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  CSV_MAX_DIM = 3,
  CSV_LINE_BUF_SZ = 4096,
  CSV_INITIAL_CAP = 1024,
};

static void set_err(char *errbuf, size_t errbuf_sz, const char *msg) {
  if (!errbuf || errbuf_sz == 0) return;
  snprintf(errbuf, errbuf_sz, "%s", msg);
}

static void set_errf(char *errbuf, size_t errbuf_sz, const char *fmt, ...) {
  va_list ap;

  if (!errbuf || errbuf_sz == 0) return;

  va_start(ap, fmt);
  vsnprintf(errbuf, errbuf_sz, fmt, ap);
  va_end(ap);
}

static int is_supported_dim(int dim) { return dim == 2 || dim == 3; }

static int dataset_is_valid(const km_dataset_t *ds) {
  return ds && ds->points && ds->n > 0 && is_supported_dim(ds->dim);
}

static void rstrip(char *s) {
  size_t n = strlen(s);
  while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' || isspace((unsigned char)s[n - 1]))) {
    s[n - 1] = '\0';
    n--;
  }
}

// Returns:
//  1: parsed successfully and set *nvals
//  0: first token is non-numeric (useful for optional headers)
// -1: malformed CSV line
static int parse_numeric_fields(const char *line, double values[CSV_MAX_DIM], int *nvals) {
  const char *cursor = line;
  int count = 0;

  while (*cursor) {
    while (*cursor && isspace((unsigned char)*cursor)) cursor++;
    if (*cursor == '\0') break;
    if (count >= CSV_MAX_DIM) return -1;

    errno = 0;
    char *end = NULL;
    const double value = strtod(cursor, &end);
    if (end == cursor) return 0;
    if (errno == ERANGE) return -1;

    values[count++] = value;
    cursor = end;

    while (*cursor && isspace((unsigned char)*cursor)) cursor++;
    if (*cursor == ',') {
      cursor++;
      continue;
    }
    break;
  }

  while (*cursor && isspace((unsigned char)*cursor)) cursor++;
  if (*cursor != '\0') return -1;

  *nvals = count;
  return 1;
}

static int grow_staging_points(double **points, size_t *cap, char *errbuf, size_t errbuf_sz) {
  const size_t new_cap = (*cap == 0) ? CSV_INITIAL_CAP : (*cap * 2);
  double *grown =
      (double *)realloc(*points, new_cap * CSV_MAX_DIM * sizeof(**points));
  if (!grown) {
    set_err(errbuf, errbuf_sz, "Sin memoria al crecer buffer de puntos");
    return 1;
  }

  *points = grown;
  *cap = new_cap;
  return 0;
}

static FILE *open_output(const char *path, char *errbuf, size_t errbuf_sz) {
  FILE *f;

  if (!path) return NULL;
  if (strcmp(path, "-") == 0) return stdout;

  f = fopen(path, "wb");
  if (!f) {
    set_errf(errbuf, errbuf_sz, "No se pudo abrir output: %s", path);
    return NULL;
  }
  return f;
}

static void close_output(FILE *f) {
  if (f && f != stdout) fclose(f);
}

static int write_assignment_header(FILE *f, int dim) {
  return fprintf(f, (dim == 2) ? "x,y,cluster\n" : "x,y,z,cluster\n") < 0;
}

static int write_centroids_header(FILE *f, int dim) {
  return fprintf(f, (dim == 2) ? "cluster,cx,cy\n" : "cluster,cx,cy,cz\n") < 0;
}

int csv_read_points(const char *path, int dim_hint, km_dataset_t *out, char *errbuf,
                    size_t errbuf_sz) {
  FILE *f = NULL;
  double *staging = NULL;
  double *compact = NULL;
  size_t cap = 0;
  size_t n = 0;
  size_t line_no = 0;
  int dim = dim_hint;
  int seen_data = 0;
  char line[CSV_LINE_BUF_SZ];

  if (!out) {
    set_err(errbuf, errbuf_sz, "csv_read_points: out is NULL");
    return 1;
  }
  memset(out, 0, sizeof(*out));

  if (!path) {
    set_err(errbuf, errbuf_sz, "csv_read_points: path is NULL");
    return 1;
  }
  if (dim_hint != 0 && !is_supported_dim(dim_hint)) {
    set_err(errbuf, errbuf_sz, "csv_read_points: dim_hint must be 0, 2, or 3");
    return 1;
  }

  f = fopen(path, "rb");
  if (!f) {
    set_errf(errbuf, errbuf_sz, "No se pudo abrir input CSV: %s", path);
    return 1;
  }

  while (fgets(line, sizeof(line), f)) {
    double values[CSV_MAX_DIM] = {0.0, 0.0, 0.0};
    int nvals = 0;
    int parse_result;

    line_no++;
    rstrip(line);
    if (line[0] == '\0') continue;

    parse_result = parse_numeric_fields(line, values, &nvals);
    if (parse_result == 0 && !seen_data) continue;
    if (parse_result <= 0) {
      set_errf(errbuf, errbuf_sz, "Linea %zu invalida en CSV", line_no);
      goto fail;
    }

    if (!seen_data) {
      if (dim == 0) {
        if (!is_supported_dim(nvals)) {
          set_errf(errbuf, errbuf_sz, "CSV debe tener 2 o 3 columnas numericas (linea %zu)",
                   line_no);
          goto fail;
        }
        dim = nvals;
      }
      seen_data = 1;
    }

    if (nvals != dim) {
      set_errf(errbuf, errbuf_sz, "CSV con %d columnas esperadas, pero linea %zu tiene %d", dim,
               line_no, nvals);
      goto fail;
    }

    if (n == cap && grow_staging_points(&staging, &cap, errbuf, errbuf_sz) != 0) goto fail;

    staging[n * CSV_MAX_DIM + 0] = values[0];
    staging[n * CSV_MAX_DIM + 1] = values[1];
    staging[n * CSV_MAX_DIM + 2] = (dim == 3) ? values[2] : 0.0;
    n++;
  }

  fclose(f);
  f = NULL;

  if (!seen_data || n == 0) {
    set_err(errbuf, errbuf_sz, "CSV sin datos numericos");
    goto fail;
  }

  compact = (double *)malloc(n * (size_t)dim * sizeof(*compact));
  if (!compact) {
    set_err(errbuf, errbuf_sz, "Sin memoria para compactar dataset");
    goto fail;
  }

  for (size_t i = 0; i < n; i++) {
    memcpy(&compact[i * (size_t)dim], &staging[i * CSV_MAX_DIM], (size_t)dim * sizeof(*compact));
  }

  free(staging);
  out->dim = dim;
  out->n = n;
  out->points = compact;
  return 0;

fail:
  free(staging);
  free(compact);
  if (f) fclose(f);
  return 1;
}

void km_dataset_free(km_dataset_t *ds) {
  if (!ds) return;
  free(ds->points);
  ds->points = NULL;
  ds->n = 0;
  ds->dim = 0;
}

int csv_write_assignments(const char *path, const km_dataset_t *ds, const int *assignments,
                          char *errbuf, size_t errbuf_sz) {
  FILE *f;

  if (!path) return 0;
  if (!dataset_is_valid(ds) || !assignments) {
    set_err(errbuf, errbuf_sz, "csv_write_assignments: argumentos invalidos");
    return 1;
  }

  f = open_output(path, errbuf, errbuf_sz);
  if (!f) return 1;

  if (write_assignment_header(f, ds->dim) != 0) {
    set_err(errbuf, errbuf_sz, "No se pudo escribir header de assignments");
    close_output(f);
    return 1;
  }

  for (size_t i = 0; i < ds->n; i++) {
    const double *point = &ds->points[i * (size_t)ds->dim];
    int rc;

    if (ds->dim == 2) {
      rc = fprintf(f, "%.17g,%.17g,%d\n", point[0], point[1], assignments[i]);
    } else {
      rc = fprintf(f, "%.17g,%.17g,%.17g,%d\n", point[0], point[1], point[2], assignments[i]);
    }
    if (rc < 0) {
      set_err(errbuf, errbuf_sz, "No se pudo escribir archivo de assignments");
      close_output(f);
      return 1;
    }
  }

  close_output(f);
  return 0;
}

int csv_write_centroids(const char *path, int dim, int k, const double *centroids,
                        char *errbuf, size_t errbuf_sz) {
  FILE *f;

  if (!path) return 0;
  if (!centroids || !is_supported_dim(dim) || k <= 0) {
    set_err(errbuf, errbuf_sz, "csv_write_centroids: argumentos invalidos");
    return 1;
  }

  f = open_output(path, errbuf, errbuf_sz);
  if (!f) return 1;

  if (write_centroids_header(f, dim) != 0) {
    set_err(errbuf, errbuf_sz, "No se pudo escribir header de centroides");
    close_output(f);
    return 1;
  }

  for (int c = 0; c < k; c++) {
    const double *centroid = &centroids[(size_t)c * (size_t)dim];
    int rc;

    if (dim == 2) {
      rc = fprintf(f, "%d,%.17g,%.17g\n", c, centroid[0], centroid[1]);
    } else {
      rc = fprintf(f, "%d,%.17g,%.17g,%.17g\n", c, centroid[0], centroid[1], centroid[2]);
    }
    if (rc < 0) {
      set_err(errbuf, errbuf_sz, "No se pudo escribir archivo de centroides");
      close_output(f);
      return 1;
    }
  }

  close_output(f);
  return 0;
}
