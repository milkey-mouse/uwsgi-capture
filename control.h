#pragma once

#include "v4l.h"
#include <linux/videodev2.h>

int v4l_get_control(int fd, unsigned int id);
int v4l_set_control(int fd, unsigned int id, int value, capture_context *ctx);
void v4l_enumerate_controls(int fd, capture_context *ctx);
