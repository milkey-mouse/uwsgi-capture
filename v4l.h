#pragma once

#include "uwsgiwrap.h"
#include <linux/videodev2.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct {
  struct v4l2_queryctrl ctrl;
  struct v4l2_querymenu *menuitems;
  int class_id;
  int value;
} v4l_control_meta;

typedef struct {
  uint16_t quality, fps;
  bool hf;
  char *name;
  char *path;
  uint16_t resolution[2];
  v4l_control_meta *controls;
  int control_count;
  struct uwsgi_sharedarea *sa;
} capture_context;

void capture_ctx_init(capture_context *ctx);

int capture_ctx_v4l_init(capture_context *ctx);
int capture_ctx_v4l_shutdown(capture_context *ctx);
int capture_ctx_process(capture_context *contexts, uint8_t length);
