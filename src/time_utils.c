#include "time_utils.h"

#include <time.h>

// Usa un reloj monotono para que cambios del reloj del sistema no afecten las mediciones.
double now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

