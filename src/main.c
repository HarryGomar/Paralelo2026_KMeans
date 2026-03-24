#include "csv_io.h"
#include "kmeans.h"
#include "time_utils.h"

#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _OPENMP
#include <omp.h>
#endif

static void usage(FILE *out) {
  fprintf(out,
          "Uso:\n"
          "  ./kmeans --input data.csv --k 5 --mode serial|omp [opciones]\n"
          "\n"
          "Opciones:\n"
          "  --input PATH           CSV de entrada con puntos (2D o 3D)\n"
          "  --k INT                Número de clusters (requerido)\n"
          "  --mode serial|omp      Modo de ejecución (default: serial)\n"
          "  --threads INT          Número de hilos (solo modo omp)\n"
          "  --dim 2|3              Dimensión (si no, se infiere del CSV)\n"
          "  --max-iters INT        Máximo de iteraciones (default: 300)\n"
          "  --tol FLOAT            Tolerancia de convergencia (default: 1e-4)\n"
          "  --seed UINT64          Semilla para inicialización reproducible (default: 42)\n"
          "  --out PATH             Output CSV por punto (x,y[,z],cluster). Opcional\n"
          "  --centroids PATH       Output CSV de centroides (cluster,cx,cy[,cz]). Opcional\n"
          "  --log-csv PATH         Append a experiments.csv (con header si no existe)\n"
          "  --run-idx INT          Índice de corrida (para experiments.csv)\n"
          "  -h, --help             Mostrar ayuda\n"
          "\n"
          "Ejemplo:\n"
          "  ./kmeans --input data.csv --k 5 --mode omp --threads 8 --max-iters 300 --tol 1e-4 \\\n"
          "          --seed 42 --out out.csv --centroids centroids.csv\n");
}

static int parse_int(const char *s, int *out) {
  if (!s || !out) return 1;
  errno = 0;
  char *end = NULL;
  long v = strtol(s, &end, 10);
  if (errno != 0 || end == s || *end != '\0') return 1;
  if (v < INT32_MIN || v > INT32_MAX) return 1;
  *out = (int)v;
  return 0;
}

static int parse_u64(const char *s, uint64_t *out) {
  if (!s || !out) return 1;
  errno = 0;
  char *end = NULL;
  unsigned long long v = strtoull(s, &end, 10);
  if (errno != 0 || end == s || *end != '\0') return 1;
  *out = (uint64_t)v;
  return 0;
}

static int parse_double(const char *s, double *out) {
  if (!s || !out) return 1;
  errno = 0;
  char *end = NULL;
  double v = strtod(s, &end);
  if (errno != 0 || end == s || *end != '\0') return 1;
  *out = v;
  return 0;
}

static km_mode_t parse_mode(const char *s, int *ok) {
  *ok = 1;
  if (!s) {
    *ok = 0;
    return KM_MODE_SERIAL;
  }
  if (strcmp(s, "serial") == 0) return KM_MODE_SERIAL;
  if (strcmp(s, "omp") == 0) return KM_MODE_OMP;
  *ok = 0;
  return KM_MODE_SERIAL;
}

static int file_exists(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) return 0;
  fclose(f);
  return 1;
}

static int append_experiment_row(const char *path, int dim, size_t n, int k, km_mode_t mode,
                                 int threads, int run_idx, int iters, double kernel_ms,
                                 double total_ms, char *errbuf, size_t errbuf_sz) {
  if (!path) return 0;
  const int needs_header = !file_exists(path);
  FILE *f = fopen(path, "ab");
  if (!f) {
    snprintf(errbuf, errbuf_sz, "No se pudo abrir log CSV: %s", path);
    return 1;
  }
  if (needs_header) {
    fprintf(f, "dim,N,k,mode,threads,run_idx,iters,kernel_ms,total_ms\n");
  }
  fprintf(f, "%d,%zu,%d,%s,%d,%d,%d,%.3f,%.3f\n", dim, n, k,
          (mode == KM_MODE_OMP) ? "omp" : "serial", threads, run_idx, iters, kernel_ms, total_ms);
  fclose(f);
  return 0;
}

int main(int argc, char **argv) {
  const char *input_path = NULL;
  const char *out_path = NULL;
  const char *centroids_path = NULL;
  const char *log_csv_path = NULL;

  km_params_t params;
  params.k = 0;
  params.max_iters = 300;
  params.tol = 1e-4;
  params.seed = 42;

  int dim_hint = 0;
  int threads = 0;
  int run_idx = 0;
  km_mode_t mode = KM_MODE_SERIAL;

  static struct option long_opts[] = {
      {"input", required_argument, NULL, 0},
      {"k", required_argument, NULL, 0},
      {"mode", required_argument, NULL, 0},
      {"threads", required_argument, NULL, 0},
      {"dim", required_argument, NULL, 0},
      {"max-iters", required_argument, NULL, 0},
      {"tol", required_argument, NULL, 0},
      {"seed", required_argument, NULL, 0},
      {"out", required_argument, NULL, 0},
      {"centroids", required_argument, NULL, 0},
      {"log-csv", required_argument, NULL, 0},
      {"run-idx", required_argument, NULL, 0},
      {"help", no_argument, NULL, 'h'},
      {NULL, 0, NULL, 0},
  };

  int opt;
  int opt_idx = 0;
  while ((opt = getopt_long(argc, argv, "h", long_opts, &opt_idx)) != -1) {
    if (opt == 'h') {
      usage(stdout);
      return 0;
    }
    if (opt != 0) continue;

    const char *name = long_opts[opt_idx].name;
    if (strcmp(name, "input") == 0) {
      input_path = optarg;
    } else if (strcmp(name, "k") == 0) {
      if (parse_int(optarg, &params.k) != 0 || params.k <= 0) {
        fprintf(stderr, "Error: --k debe ser un entero > 0\n");
        return 2;
      }
    } else if (strcmp(name, "mode") == 0) {
      int ok = 0;
      mode = parse_mode(optarg, &ok);
      if (!ok) {
        fprintf(stderr, "Error: --mode debe ser 'serial' u 'omp'\n");
        return 2;
      }
    } else if (strcmp(name, "threads") == 0) {
      if (parse_int(optarg, &threads) != 0 || threads <= 0) {
        fprintf(stderr, "Error: --threads debe ser un entero > 0\n");
        return 2;
      }
    } else if (strcmp(name, "dim") == 0) {
      if (parse_int(optarg, &dim_hint) != 0 || (dim_hint != 2 && dim_hint != 3)) {
        fprintf(stderr, "Error: --dim debe ser 2 o 3\n");
        return 2;
      }
    } else if (strcmp(name, "max-iters") == 0) {
      if (parse_int(optarg, &params.max_iters) != 0 || params.max_iters <= 0) {
        fprintf(stderr, "Error: --max-iters debe ser un entero > 0\n");
        return 2;
      }
    } else if (strcmp(name, "tol") == 0) {
      if (parse_double(optarg, &params.tol) != 0 || params.tol < 0.0) {
        fprintf(stderr, "Error: --tol debe ser un float >= 0\n");
        return 2;
      }
    } else if (strcmp(name, "seed") == 0) {
      if (parse_u64(optarg, &params.seed) != 0) {
        fprintf(stderr, "Error: --seed debe ser un entero sin signo (uint64)\n");
        return 2;
      }
    } else if (strcmp(name, "out") == 0) {
      out_path = optarg;
    } else if (strcmp(name, "centroids") == 0) {
      centroids_path = optarg;
    } else if (strcmp(name, "log-csv") == 0) {
      log_csv_path = optarg;
    } else if (strcmp(name, "run-idx") == 0) {
      if (parse_int(optarg, &run_idx) != 0 || run_idx < 0) {
        fprintf(stderr, "Error: --run-idx debe ser un entero >= 0\n");
        return 2;
      }
    } else {
      fprintf(stderr, "Error: opción desconocida\n");
      return 2;
    }
  }

  if (!input_path) {
    fprintf(stderr, "Error: falta --input\n");
    usage(stderr);
    return 2;
  }
  if (params.k <= 0) {
    fprintf(stderr, "Error: falta --k\n");
    usage(stderr);
    return 2;
  }

  if (mode == KM_MODE_OMP) {
    if (threads <= 0) {
#ifdef _OPENMP
      threads = omp_get_max_threads();
#else
      threads = 1;
#endif
    }
#ifdef _OPENMP
    if (threads > omp_get_max_threads()) threads = omp_get_max_threads();
#endif
  } else {
    threads = 1;
  }

  char errbuf[256];
  double t_total0 = now_ms();

  km_dataset_t ds;
  if (csv_read_points(input_path, dim_hint, &ds, errbuf, sizeof(errbuf)) != 0) {
    fprintf(stderr, "Error leyendo CSV: %s\n", errbuf);
    return 1;
  }
  if (params.k > (int)ds.n) {
    fprintf(stderr, "Error: k=%d no puede ser mayor que N=%zu\n", params.k, ds.n);
    km_dataset_free(&ds);
    return 2;
  }

  int *assignments = (int *)malloc(ds.n * sizeof(int));
  double *centroids = (double *)malloc((size_t)params.k * (size_t)ds.dim * sizeof(double));
  if (!assignments || !centroids) {
    fprintf(stderr, "Error: sin memoria\n");
    free(assignments);
    free(centroids);
    km_dataset_free(&ds);
    return 1;
  }
  for (size_t i = 0; i < ds.n; i++) assignments[i] = -1;

  km_stats_t stats;
  memset(&stats, 0, sizeof(stats));

  double t_kernel0 = now_ms();
  int rc = 0;
  if (mode == KM_MODE_SERIAL) {
    rc = km_run_serial(&ds, &params, assignments, centroids, &stats);
  } else {
    rc = km_run_omp(&ds, &params, threads, assignments, centroids, &stats);
  }
  double t_kernel1 = now_ms();

  if (rc != 0) {
    fprintf(stderr, "Error: fallo en kmeans\n");
    free(assignments);
    free(centroids);
    km_dataset_free(&ds);
    return 1;
  }

  // Optional outputs (excluded from kernel time, included in total).
  if (csv_write_assignments(out_path, &ds, assignments, errbuf, sizeof(errbuf)) != 0) {
    fprintf(stderr, "Error escribiendo --out: %s\n", errbuf);
    free(assignments);
    free(centroids);
    km_dataset_free(&ds);
    return 1;
  }
  if (csv_write_centroids(centroids_path, ds.dim, params.k, centroids, errbuf, sizeof(errbuf)) !=
      0) {
    fprintf(stderr, "Error escribiendo --centroids: %s\n", errbuf);
    free(assignments);
    free(centroids);
    km_dataset_free(&ds);
    return 1;
  }

  double t_total1 = now_ms();
  const double kernel_ms = t_kernel1 - t_kernel0;
  const double total_ms = t_total1 - t_total0;

  if (append_experiment_row(log_csv_path, ds.dim, ds.n, params.k, mode, threads, run_idx,
                            stats.iters, kernel_ms, total_ms, errbuf, sizeof(errbuf)) != 0) {
    fprintf(stderr, "Error escribiendo --log-csv: %s\n", errbuf);
    free(assignments);
    free(centroids);
    km_dataset_free(&ds);
    return 1;
  }

  printf(
      "modo=%s threads=%d N=%zu dim=%d k=%d iteraciones=%d tiempo_kernel_ms=%.3f "
      "tiempo_total_ms=%.3f\n",
      (mode == KM_MODE_OMP) ? "omp" : "serial", threads, ds.n, ds.dim, params.k, stats.iters,
      kernel_ms, total_ms);

  free(assignments);
  free(centroids);
  km_dataset_free(&ds);
  return 0;
}
