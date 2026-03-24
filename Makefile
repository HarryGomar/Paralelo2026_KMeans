CC ?= gcc

BIN := bin/kmeans
BUILD_DIR := build

CPPFLAGS := -Iinclude
CFLAGS := -O3 -march=native -std=c11 -Wall -Wextra -Wshadow -Wconversion -pedantic
LDLIBS := -lm

# Set OPENMP=0 to build without OpenMP support.
OPENMP ?= 1
ifeq ($(OPENMP),1)
  CFLAGS += -fopenmp
endif

SRC := \
  src/main.c \
  src/csv_io.c \
  src/time_utils.c \
  src/rng.c \
  src/kmeans_common.c \
  src/kmeans_serial.c \
  src/kmeans_omp.c

OBJ := $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(SRC))

.PHONY: all clean

all: $(BIN)

$(BIN): $(OBJ) | bin
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@ $(LDLIBS)

$(BUILD_DIR)/%.o: src/%.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

bin:
	mkdir -p bin

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	rm -rf bin build

