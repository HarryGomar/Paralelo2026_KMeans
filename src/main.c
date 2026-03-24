#include "csv_io.h"
#include "kmeans.h"
#include "time_utils.h"

#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <stdint.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _OPENMP
#include <omp.h>
#endif

typedef struct {
  const char *input_path;
  const char *out_path;
  const char *centroids_path;
  const char *log_csv_path;
  km_params_t params;
  km_mode_t mode;
  int threads;
  int dim_hint;
  int run_idx;
} app_config_t;

static void usage(FILE *out) {
  fprintf(out,
          "Uso:\n"
          "  ./kmeans --input data.csv --k 5 --mode serial|omp [opciones]\n"
          "\n"
          "Opciones:\n"
          "  --input PATH           CSV de entrada con puntos (2D o 3D)\n"
          "  --k INT                Numero de clusters (requerido)\n"
          "  --mode serial|omp      Modo de ejecucion (default: serial)\n"
          "  --threads INT          Numero de hilos (solo modo omp)\n"
          "  --dim 2|3              Dimension (si no, se infiere del CSV)\n"
          "  --max-iters INT        Maximo de iteraciones (default: 300)\n"
          "  --tol FLOAT            Tolerancia de convergencia (default: 1e-4)\n"
          "  --seed UINT64          Semilla reproducible (default: 42)\n"
          "  --out PATH             Output CSV por punto (x,y[,z],cluster). Opcional\n"
          "  --centroids PATH       Output CSV de centroides (cluster,cx,cy[,cz]). Opcional\n"
          "  --log-csv PATH         Append a experiments.csv (con header si no existe)\n"
          "  --run-idx INT          Indice de corrida (para experiments.csv)\n"
          "  -h, --help             Mostrar ayuda\n");
}

static void app_config_init(app_config_t *cfg) {
  memset(cfg, 0, sizeof(*cfg));
  cfg->params.max_iters = 300;
  cfg->params.tol = 1e-4;
  cfg->params.seed = 42;
  cfg->mode = KM_MODE_SERIAL;
}

static int parse_int(const char *text, int *out) {
  char *end = NULL;
  long value;

  if (!text || !out) return 1;
  errno = 0;
  value = strtol(text, &end, 10);
  if (errno != 0 || end == text || *end != '\0') return 1;
  if (value < INT_MIN || value > INT_MAX) return 1;

  *out = (int)value;
  return 0;
}

static int parse_u64(const char *text, uint64_t *out) {
  char *end = NULL;
  unsigned long long value;

  if (!text || !out) return 1;
  errno = 0;
  value = strtoull(text, &end, 10);
  if (errno != 0 || end == text || *end != '\0') return 1;

  *out = (uint64_t)value;
  return 0;
}

static int parse_double(const char *text, double *out) {
  char *end = NULL;
  double value;

  if (!text || !out) return 1;
  errno = 0;
  value = strtod(text, &end);
  if (errno != 0 || end == text || *end != '\0') return 1;

  *out = value;
  return 0;
}

static int parse_mode(const char *text, km_mode_t *mode) {
  if (!text || !mode) return 1;
  if (strcmp(text, "serial") == 0) {
    *mode = KM_MODE_SERIAL;
    return 0;
  }
  if (strcmp(text, "omp") == 0) {
    *mode = KM_MODE_OMP;
    return 0;
  }
  return 1;
}

static int file_exists(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) return 0;
  fclose(f);
  return 1;
}

static int file_is_empty(const char *path) {
  struct stat st;

  if (stat(path, &st) != 0) return 1;
  return st.st_size == 0;
}

static int append_experiment_row(const char *path, int dim, size_t n, int k, km_mode_t mode,
                                 int threads, int run_idx, int iters, double kernel_ms,
                                 double total_ms, char *errbuf, size_t errbuf_sz) {
  FILE *f;
  int needs_header;

  if (!path) return 0;
  needs_header = !file_exists(path);

  f = fopen(path, "ab");
  if (!f) {
    snprintf(errbuf, errbuf_sz, "No se pudo abrir log CSV: %s", path);
    return 1;
  }

  if (needs_header || file_is_empty(path)) {
    fprintf(f, "dim,N,k,mode,threads,run_idx,iters,kernel_ms,total_ms\n");
  }

  fprintf(f, "%d,%zu,%d,%s,%d,%d,%d,%.3f,%.3f\n", dim, n, k,
          (mode == KM_MODE_OMP) ? "omp" : "serial", threads, run_idx, iters, kernel_ms, total_ms);
  fclose(f);
  return 0;
}

static int parse_options(int argc, char **argv, app_config_t *cfg) {
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
      return 1;
    }
    if (opt != 0) continue;

    switch (opt_idx) {
      case 0:
        cfg->input_path = optarg;
        break;
      case 1:
        if (parse_int(optarg, &cfg->params.k) != 0 || cfg->params.k <= 0) {
          fprintf(stderr, "Error: --k debe ser un entero > 0\n");
          return -1;
        }
        break;
      case 2:
        if (parse_mode(optarg, &cfg->mode) != 0) {
          fprintf(stderr, "Error: --mode debe ser 'serial' u 'omp'\n");
          return -1;
        }
        break;
      case 3:
        if (parse_int(optarg, &cfg->threads) != 0 || cfg->threads <= 0) {
          fprintf(stderr, "Error: --threads debe ser un entero > 0\n");
          return -1;
        }
        break;
      case 4:
        if (parse_int(optarg, &cfg->dim_hint) != 0 || (cfg->dim_hint != 2 && cfg->dim_hint != 3)) {
          fprintf(stderr, "Error: --dim debe ser 2 o 3\n");
          return -1;
        }
        break;
      case 5:
        if (parse_int(optarg, &cfg->params.max_iters) != 0 || cfg->params.max_iters <= 0) {
          fprintf(stderr, "Error: --max-iters debe ser un entero > 0\n");
          return -1;
        }
        break;
      case 6:
        if (parse_double(optarg, &cfg->params.tol) != 0 || cfg->params.tol < 0.0) {
          fprintf(stderr, "Error: --tol debe ser un float >= 0\n");
          return -1;
        }
        break;
      case 7:
        if (parse_u64(optarg, &cfg->params.seed) != 0) {
          fprintf(stderr, "Error: --seed debe ser un entero sin signo (uint64)\n");
          return -1;
        }
        break;
      case 8:
        cfg->out_path = optarg;
        break;
      case 9:
        cfg->centroids_path = optarg;
        break;
      case 10:
        cfg->log_csv_path = optarg;
        break;
      case 11:
        if (parse_int(optarg, &cfg->run_idx) != 0 || cfg->run_idx < 0) {
          fprintf(stderr, "Error: --run-idx debe ser un entero >= 0\n");
          return -1;
        }
        break;
      default:
        fprintf(stderr, "Error: opcion desconocida\n");
        return -1;
    }
  }

  if (!cfg->input_path) {
    fprintf(stderr, "Error: falta --input\n");
    usage(stderr);
    return -1;
  }
  if (cfg->params.k <= 0) {
    fprintf(stderr, "Error: falta --k\n");
    usage(stderr);
    return -1;
  }

  return 0;
}

static int resolve_threads(app_config_t *cfg) {
  if (cfg->mode != KM_MODE_OMP) {
    cfg->threads = 1;
    return 0;
  }

  if (cfg->threads > 0) return 0;

#ifdef _OPENMP
  cfg->threads = omp_get_max_threads();
#else
  cfg->threads = 1;
#endif
  return 0;
}

static int run_kmeans_mode(const app_config_t *cfg, const km_dataset_t *ds, int *assignments,
                           double *centroids, km_stats_t *stats) {
  if (cfg->mode == KM_MODE_OMP) {
    return km_run_omp(ds, &cfg->params, cfg->threads, assignments, centroids, stats);
  }
  return km_run_serial(ds, &cfg->params, assignments, centroids, stats);
}

static int write_outputs(const app_config_t *cfg, const km_dataset_t *ds, const int *assignments,
                         const double *centroids, char *errbuf, size_t errbuf_sz) {
  if (csv_write_assignments(cfg->out_path, ds, assignments, errbuf, errbuf_sz) != 0) {
    return 1;
  }
  if (csv_write_centroids(cfg->centroids_path, ds->dim, cfg->params.k, centroids, errbuf,
                          errbuf_sz) != 0) {
    return 1;
  }
  return 0;
}

int main(int argc, char **argv) {
  app_config_t cfg;
  km_dataset_t ds;
  km_stats_t stats;
  int *assignments = NULL;
  double *centroids = NULL;
  char errbuf[256];
  int rc = 1;
  double total_start;
  double kernel_start;
  double kernel_end;
  double total_end;
  double kernel_ms;
  double total_ms;

  app_config_init(&cfg);
  memset(&ds, 0, sizeof(ds));
  memset(&stats, 0, sizeof(stats));

  rc = parse_options(argc, argv, &cfg);
  if (rc > 0) return 0;
  if (rc < 0) return 2;

  resolve_threads(&cfg);
  total_start = now_ms();

  if (csv_read_points(cfg.input_path, cfg.dim_hint, &ds, errbuf, sizeof(errbuf)) != 0) {
    fprintf(stderr, "Error leyendo CSV: %s\n", errbuf);
    return 1;
  }
  if (cfg.params.k > (int)ds.n) {
    fprintf(stderr, "Error: k=%d no puede ser mayor que N=%zu\n", cfg.params.k, ds.n);
    rc = 2;
    goto cleanup;
  }

  assignments = (int *)malloc(ds.n * sizeof(*assignments));
  centroids =
      (double *)malloc((size_t)cfg.params.k * (size_t)ds.dim * sizeof(*centroids));
  if (!assignments || !centroids) {
    fprintf(stderr, "Error: sin memoria\n");
    goto cleanup;
  }

  kernel_start = now_ms();
  if (run_kmeans_mode(&cfg, &ds, assignments, centroids, &stats) != 0) {
    fprintf(stderr, "Error: fallo en kmeans\n");
    goto cleanup;
  }
  kernel_end = now_ms();

  if (write_outputs(&cfg, &ds, assignments, centroids, errbuf, sizeof(errbuf)) != 0) {
    fprintf(stderr, "Error escribiendo outputs: %s\n", errbuf);
    goto cleanup;
  }

  total_end = now_ms();
  kernel_ms = kernel_end - kernel_start;
  total_ms = total_end - total_start;

  if (append_experiment_row(cfg.log_csv_path, ds.dim, ds.n, cfg.params.k, cfg.mode, cfg.threads,
                            cfg.run_idx, stats.iters, kernel_ms, total_ms, errbuf,
                            sizeof(errbuf)) != 0) {
    fprintf(stderr, "Error escribiendo --log-csv: %s\n", errbuf);
    goto cleanup;
  }

  printf(
      "modo=%s threads=%d N=%zu dim=%d k=%d iteraciones=%d tiempo_kernel_ms=%.3f "
      "tiempo_total_ms=%.3f\n",
      (cfg.mode == KM_MODE_OMP) ? "omp" : "serial", cfg.threads, ds.n, ds.dim, cfg.params.k,
      stats.iters, kernel_ms, total_ms);

  rc = 0;

cleanup:
  free(assignments);
  free(centroids);
  km_dataset_free(&ds);
  return rc;
}
