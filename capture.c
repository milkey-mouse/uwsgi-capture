#include "capture.h"
#include "util.h"
#include "uwsgiwrap.h"
#include "v4l.h"
#include <stdbool.h>

static capture_context cmdline_ctx = {.quality = 80,
                                      .fps = 255,
                                      .hf = false,
                                      .name = "Unknown",
                                      .path = NULL,
                                      .resolution = {640, 480}};

static capture_context *capture_contexts = NULL;
static struct uwsgi_lock_item *capture_lock;
static uint8_t contexts_length = 0;
static uint8_t contexts_size = 0;

int add_capture_ctx(capture_context *ctx) {
  int ret = capture_ctx_v4l_init(ctx);
  if (ret < 0) {
    return ret;
  }

  uwsgi_wlock(capture_lock);
  if (contexts_length == contexts_size) {
    uint8_t new_size = (contexts_size == 0) ? 1 : (contexts_size * 2);
    capture_context *new_buf = (capture_context *)realloc(
        capture_contexts, new_size * sizeof(capture_context));
    if (new_buf == NULL) {
      uwsgi_rwunlock(capture_lock);
      uwsgi_error("could not realloc() capture contexts array");
      return -1;
    }
    capture_contexts = new_buf;
    contexts_size = new_size;
  }
  uint8_t idx = contexts_length++;
  capture_contexts[idx] = *ctx;
  uwsgi_rwunlock(capture_lock);
  return idx;
}

int remove_capture_ctx(uint8_t id) {
  if (id >= contexts_length) {
    return -1;
  }

  uwsgi_wlock(capture_lock);
  int ret = capture_ctx_v4l_shutdown(&capture_contexts[id]);
  if (ret != 0) {
    uwsgi_rwunlock(capture_lock);
    return ret;
  }

  memmove(&capture_contexts[id], &capture_contexts[id + 1],
          (--contexts_length - id) * sizeof(capture_context));
  uwsgi_rwunlock(capture_lock);
  return 0;
}

static struct uwsgi_option capture_options[] = {
    {"v4l-device", required_argument, 0,
     "capture from the specified v4l device", uwsgi_opt_set_str_and_add_mule,
     &cmdline_ctx.path, 0},
    {"resolution", required_argument, 0, "resolution of the captured video",
     uwsgi_opt_set_resolution, cmdline_ctx.resolution, 0},
    {"quality", required_argument, 0, "JPEG quality (0-100) of each frame",
     uwsgi_opt_set_8bit, &cmdline_ctx.quality, 0},
    {"fps", required_argument, 0, "number of frames to generate per second",
     uwsgi_opt_set_8bit, &cmdline_ctx.fps, 0},
    {"hf", no_argument, 0, "flip the video horizontally", uwsgi_opt_true,
     &cmdline_ctx.hf, 0},
    {NULL, 0, 0, NULL, NULL, NULL, 0},
};

static int capture_init() {
  capture_lock = uwsgi_rwlock_init("capture_contexts");
  if (capture_lock == NULL) {
    uwsgi_fatal_error("could not initialize lock for list of capture contexts");
  }

  if (cmdline_ctx.path != NULL && add_capture_ctx(&cmdline_ctx) < 0) {
    exit(1);
  }
  return 0;
}

int capture_loop() {
  while (true) {
    uwsgi_rlock(capture_lock);
    int ret = capture_ctx_process(capture_contexts, contexts_length);
    uwsgi_rwunlock(capture_lock);
    if (ret < 0) {
      return ret;
    }
  }
  return 0;
}

struct uwsgi_plugin capture_plugin = {
    .name = "capture", .options = capture_options, .init = capture_init
    // capture_loop is automatically added as a mule in util.c
};
