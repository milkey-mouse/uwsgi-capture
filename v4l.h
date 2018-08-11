#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
  uint16_t quality, fps;
  bool hf;
  char *name;
  char *path;
  uint16_t resolution[2];
} capture_context;
