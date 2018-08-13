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
  bool set;
  int value;
} control_option;

typedef struct {
  bool set;
  int value;
  int manual_value;
} control_option_auto;

typedef struct {
  control_option quality, sh, co, br, sa, wb, bk, rot, hf, vf, pl, gain_auto,
      gain, cagc_auto, cagc, cb_auto, cb;
  control_option_auto ex;
  v4l2_std_id tvnorm;
} control_options;

typedef struct {
  uint16_t quality, fps;
  char *name;
  char *path;
  uint16_t resolution[2];
  control_options control_options;
  v4l_control_meta *controls;
  int control_count;
  struct uwsgi_sharedarea *sa;
} capture_context;

void capture_ctx_init(capture_context *ctx);

int capture_ctx_v4l_init(capture_context *ctx);
int capture_ctx_v4l_shutdown(capture_context *ctx);
int capture_ctx_process(capture_context *contexts, uint8_t length);
