#pragma once

#include "v4l.h"
#include <stdint.h>

int add_capture_ctx(capture_context *ctx);
int remove_capture_ctx(uint8_t id);
int capture_loop();
