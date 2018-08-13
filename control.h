#pragma once

#include "v4l.h"

int v4l_get_control(capture_context *ctx, unsigned int id);
int v4l_set_control(capture_context *ctx, unsigned int id, int value);
int v4l_reset_control(capture_context *ctx, unsigned int id);

int v4l_setup_controls(capture_context *ctx);
void v4l_enumerate_controls(capture_context *ctx);
