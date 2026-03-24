#include "csv_io.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

static void set_err(char *errbuf, size_t errbuf_sz, const char *msg) {
  if (!errbuf || errbuf_sz == 0) return;
  snprintf(errbuf, errbuf_sz, "%s", msg);
}

static void set_errf(char *errbuf, size_t errbuf_sz, const char *fmt, ...) {
  if (!errbuf || errbuf_sz == 0) return;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(errbuf, errbuf_sz, fmt, ap);
  va_end(ap);
}

static void rstrip(char *s) {
  size_t n = strlen(s);
  while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' || isspace((unsigned char)s[n - 1]))) {
    s[n - 1] = '\0';
    n--;
  }
}

// Parses up to 3 doubles separated by commas. Returns:
//  1: parsed successfully with *nvals set
//  0: no numeric parse (likely header)
// -1: malformed line
static int parse_numeric_fields(const char *line, double vals[3], int *nvals) {
  const char *p = line;
  int n = 0;
  while (*p) {
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p == '\0') break;
    if (n >= 3) return -1;

    errno = 0;
    char *end = NULL;
    double v = strtod(p, &end);
    if (end == p) return 0;
    if (errno == ERANGE) return -1;
    vals[n++] = v;
    p = end;

    while (*p && isspace((unsigned char)*p)) p++;
    if (*p == ',') {
      p++;
      continue;
    }
    break;
  }

  while (*p && isspace((unsigned char)*p)) p++;
  if (*p != '\0') return -1;

  *nvals = n;
  return 1;
}

int csv_read_points(const char *path, int dim_hint, km_dataset_t *out, char *errbuf,
                    size_t errbuf_sz) {
  if (!out) {
    set_err(errbuf, errbuf_sz, "csv_read_points: out is NULL");
    return 1;
  }
  memset(out, 0, sizeof(*out));

  if (!path) {
    set_err(errbuf, errbuf_sz, "csv_read_points: path is NULL");
    return 1;
  }
  if (dim_hint != 0 && dim_hint != 2 && dim_hint != 3) {
    set_err(errbuf, errbuf_sz, "csv_read_points: dim_hint must be 0, 2, or 3");
    return 1;
  }

  FILE *f = fopen(path, "rb");
  if (!f) {
    set_errf(errbuf, errbuf_sz, "No se pudo abrir input CSV: %s", path);
    return 1;
  }

  size_t cap = 1024;
  double *points = (double *)malloc(cap * 3 * sizeof(double));
  if (!points) {
    fclose(f);
    set_err(errbuf, errbuf_sz, "Sin memoria para cargar puntos");
    return 1;
  }

  int dim = dim_hint;
  size_t n = 0;
  int seen_data = 0;

  char buf[4096];
  size_t line_no = 0;
  while (fgets(buf, (int)sizeof(buf), f)) {
    line_no++;
    rstrip(buf);
    if (buf[0] == '\0') continue;

    double vals[3] = {0, 0, 0};
    int nvals = 0;
    int pr = parse_numeric_fields(buf, vals, &nvals);
    if (pr == 0 && !seen_data) {
      // Header line before any data
      continue;
    }
    if (pr <= 0) {
      free(points);
      fclose(f);
      set_errf(errbuf, errbuf_sz, "Línea %zu inválida en CSV", line_no);
      return 1;
    }

    if (!seen_data) {
      if (dim == 0) {
        if (nvals == 2 || nvals == 3) {
          dim = nvals;
        } else {
          free(points);
          fclose(f);
          set_errf(errbuf, errbuf_sz, "CSV debe tener 2 o 3 columnas numéricas (línea %zu)",
                   line_no);
          return 1;
        }
      }
      seen_data = 1;
    }

    if (nvals != dim) {
      free(points);
      fclose(f);
      set_errf(errbuf, errbuf_sz, "CSV con %d columnas esperadas, pero línea %zu tiene %d",
               dim, line_no, nvals);
      return 1;
    }

    if (n == cap) {
      cap *= 2;
      double *np = (double *)realloc(points, cap * 3 * sizeof(double));
      if (!np) {
        free(points);
        fclose(f);
        set_err(errbuf, errbuf_sz, "Sin memoria al crecer buffer de puntos");
        return 1;
      }
      points = np;
    }

    points[n * 3 + 0] = vals[0];
    points[n * 3 + 1] = vals[1];
    points[n * 3 + 2] = (dim == 3) ? vals[2] : 0.0;
    n++;
  }

  fclose(f);

  if (!seen_data || n == 0) {
    free(points);
    set_err(errbuf, errbuf_sz, "CSV sin datos numéricos");
    return 1;
  }

  // Compact to n*dim (store in row-major with exact dim)
  double *compact = (double *)malloc(n * (size_t)dim * sizeof(double));
  if (!compact) {
    free(points);
    set_err(errbuf, errbuf_sz, "Sin memoria para compactar dataset");
    return 1;
  }
  for (size_t i = 0; i < n; i++) {
    compact[i * (size_t)dim + 0] = points[i * 3 + 0];
    compact[i * (size_t)dim + 1] = points[i * 3 + 1];
    if (dim == 3) compact[i * (size_t)dim + 2] = points[i * 3 + 2];
  }
  free(points);

  out->dim = dim;
  out->n = n;
  out->points = compact;
  return 0;
}

void km_dataset_free(km_dataset_t *ds) {
  if (!ds) return;
  free(ds->points);
  ds->points = NULL;
  ds->n = 0;
  ds->dim = 0;
}

static FILE *open_out(const char *path, char *errbuf, size_t errbuf_sz) {
  if (strcmp(path, "-") == 0) return stdout;
  FILE *f = fopen(path, "wb");
  if (!f) {
    set_errf(errbuf, errbuf_sz, "No se pudo abrir output: %s", path);
    return NULL;
  }
  return f;
}

int csv_write_assignments(const char *path, const km_dataset_t *ds, const int *assignments,
                          char *errbuf, size_t errbuf_sz) {
  if (!path) return 0;
  if (!ds || !ds->points || !assignments) {
    set_err(errbuf, errbuf_sz, "csv_write_assignments: argumentos inválidos");
    return 1;
  }

  FILE *f = open_out(path, errbuf, errbuf_sz);
  if (!f) return 1;

  if (ds->dim == 2) {
    fprintf(f, "x,y,cluster\n");
    for (size_t i = 0; i < ds->n; i++) {
      const double x = ds->points[i * 2 + 0];
      const double y = ds->points[i * 2 + 1];
      fprintf(f, "%.17g,%.17g,%d\n", x, y, assignments[i]);
    }
  } else {
    fprintf(f, "x,y,z,cluster\n");
    for (size_t i = 0; i < ds->n; i++) {
      const double x = ds->points[i * 3 + 0];
      const double y = ds->points[i * 3 + 1];
      const double z = ds->points[i * 3 + 2];
      fprintf(f, "%.17g,%.17g,%.17g,%d\n", x, y, z, assignments[i]);
    }
  }

  if (f != stdout) fclose(f);
  return 0;
}

int csv_write_centroids(const char *path, int dim, int k, const double *centroids,
                        char *errbuf, size_t errbuf_sz) {
  if (!path) return 0;
  if (!centroids || (dim != 2 && dim != 3) || k <= 0) {
    set_err(errbuf, errbuf_sz, "csv_write_centroids: argumentos inválidos");
    return 1;
  }

  FILE *f = open_out(path, errbuf, errbuf_sz);
  if (!f) return 1;

  if (dim == 2) {
    fprintf(f, "cluster,cx,cy\n");
    for (int c = 0; c < k; c++) {
      fprintf(f, "%d,%.17g,%.17g\n", c, centroids[(size_t)c * 2 + 0], centroids[(size_t)c * 2 + 1]);
    }
  } else {
    fprintf(f, "cluster,cx,cy,cz\n");
    for (int c = 0; c < k; c++) {
      fprintf(f, "%d,%.17g,%.17g,%.17g\n", c, centroids[(size_t)c * 3 + 0],
              centroids[(size_t)c * 3 + 1], centroids[(size_t)c * 3 + 2]);
    }
  }

  if (f != stdout) fclose(f);
  return 0;
}
