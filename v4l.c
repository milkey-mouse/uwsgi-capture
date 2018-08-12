#include "v4l.h"
#include "util.h"
#include "uwsgiwrap.h"
#include <errno.h>
#include <inttypes.h>
#include <linux/videodev2.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>

static capture_context default_ctx = {.quality = 80,
                                      .fps = 255,
                                      .hf = false,
                                      .name = "Unknown",
                                      .path = "/dev/video0",
                                      .resolution = {640, 480}};

void capture_ctx_init(capture_context *ctx) { *ctx = default_ctx; }

int capture_ctx_v4l_init(capture_context *ctx) {
  if (ctx->quality > 100) {
    ctx->quality = 100;
  }

  int fd = open(ctx->path, O_RDWR | O_NONBLOCK);
  if (fd == -1) {
    uwsgi_log("Error opening V4L interface %s\n", ctx->path);
    return -1;
  }

  struct v4l2_capability cap;
  memset(&cap, 0, sizeof(cap));
  if (xioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
    uwsgi_log("Error opening device %s: unable to query device.\n", ctx->path);
    return -1;
  }

  if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
    uwsgi_log("Error opening device %s: video capture not supported.\n",
              ctx->path);
  }

  if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
    uwsgi_log("Device %s does not support streaming I/O\n", ctx->path);
  }

  struct v4l2_format fmt;
  memset(&fmt, 0, sizeof(fmt));
  fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  fmt.fmt.pix.width = ctx->resolution[0];
  fmt.fmt.pix.height = ctx->resolution[1];
  fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
  fmt.fmt.pix.field = V4L2_FIELD_ANY;
  if (xioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
    uwsgi_log("Unable to set pixel format MJPEG @ resolution %dx%d\n",
              ctx->resolution[0], ctx->resolution[1]);
    return -1;
  }

  if (fmt.fmt.pix.width != ctx->resolution[0] ||
      fmt.fmt.pix.height != ctx->resolution[1]) {
    uwsgi_log("Specified resolution is unavailable, using %dx%d instead\n",
              fmt.fmt.pix.width, fmt.fmt.pix.height);
    ctx->resolution[0] = fmt.fmt.pix.width;
    ctx->resolution[1] = fmt.fmt.pix.height;
  }

  if (fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_MJPEG &&
      fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_JPEG) {
    uwsgi_log("Device %s does not support native MJPEG encoding\n", ctx->path);
    return -1;
  }

  struct v4l2_streamparm setfps;
  memset(&setfps, 0, sizeof(setfps));
  setfps.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

  if (xioctl(fd, VIDIOC_G_PARM, &setfps) < 0 ||
      !(setfps.parm.capture.capability & V4L2_CAP_TIMEPERFRAME)) {
    uwsgi_log("Can't change FPS for device %s\n", ctx->path);
  } else {
    memset(&setfps, 0, sizeof(setfps));
    setfps.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    setfps.parm.capture.timeperframe.numerator = 1;
    setfps.parm.capture.timeperframe.denominator = ctx->fps;

    if (xioctl(fd, VIDIOC_S_PARM, &setfps)) {
      uwsgi_log("Can't set FPS of device %s\n", ctx->path);
    }
  }

  struct v4l2_requestbuffers rb;
  rb.count = 1;
  rb.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  rb.memory = V4L2_MEMORY_MMAP;

  if (xioctl(fd, VIDIOC_REQBUFS, &rb) < 0) {
    uwsgi_log("Unable to allocate/mmap buffer for device %s\n", ctx->path);
    return -1;
  }

  struct v4l2_buffer vbuf;
  memset(&vbuf, 0, sizeof(vbuf));
  vbuf.index = 0;
  vbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  vbuf.memory = V4L2_MEMORY_MMAP;
  if (xioctl(fd, VIDIOC_QUERYBUF, &vbuf) < 0) {
    uwsgi_log("Unable to query mmap'ed buffer for device %s\n", ctx->path);
    return -1;
  }

  uint64_t area_len = vbuf.length;
  char *area = mmap(NULL, vbuf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
                    vbuf.m.offset);

  ctx->sa = uwsgi_sharedarea_init_ptr(area, area_len);
  ctx->sa->fd = fd;
  ctx->sa->honour_used = 1;

  memset(&vbuf, 0, sizeof(vbuf));
  vbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  vbuf.memory = V4L2_MEMORY_MMAP;
  vbuf.index = 0;

  if (xioctl(fd, VIDIOC_QBUF, &vbuf) < 0) {
    uwsgi_log("Unable to queue mmap'ed buffer for device %s\n", ctx->path);
    return -1;
  }

  struct v4l2_input in_struct;
  memset(&in_struct, 0, sizeof(in_struct));
  in_struct.index = 0;
  if (!xioctl(fd, VIDIOC_ENUMINPUT, &in_struct)) {
    ctx->name = strdup((const char *)in_struct.name);
  }

  int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (xioctl(fd, VIDIOC_STREAMON, &type) < 0) {
    uwsgi_log("Unable to start capture stream for device %s\n", ctx->path);
    return -1;
  }

  uwsgi_log("%s started streaming frames to sharedarea %d\n", ctx->path,
            ctx->sa->id);

  return 0;
}

int capture_ctx_v4l_shutdown(capture_context *ctx) {
  if (ctx->sa == NULL) {
    return 0;
  }

  int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (xioctl(ctx->sa->fd, VIDIOC_STREAMOFF, &type) < 0) {
    uwsgi_log("Unable to stop capture stream for device %s\n", ctx->path);
    return -1;
  }

  close(ctx->sa->fd);
  return 0;
}

int capture_ctx_process(capture_context *contexts, uint8_t length) {
  int ret, maxfd = 0;
  fd_set readset;
  do {
    FD_ZERO(&readset);
    for (uint8_t i = 0; i < length; i++) {
      int fd = contexts[i].sa->fd;
      FD_SET(fd, &readset);
      if (fd > maxfd) {
        maxfd = fd;
      }
    }
    ret = select(maxfd + 1, &readset, NULL, NULL, NULL);
  } while (ret == -1 && errno == EINTR);

  if (ret < 0) {
    uwsgi_error("select() failed");
    return -1;
  }

  for (uint8_t i = 0; i < length; i++) {
    capture_context *ctx = &contexts[i];
    if (FD_ISSET(ctx->sa->fd, &readset)) {
      struct v4l2_buffer vbuf;
      memset(&vbuf, 0, sizeof(vbuf));
      vbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      vbuf.memory = V4L2_MEMORY_MMAP;

      // dequeue buf
      uwsgi_wlock(ctx->sa->lock);
      if (xioctl(ctx->sa->fd, VIDIOC_DQBUF, &vbuf) < 0) {
        uwsgi_error("ioctl() failed");
        return -1;
      }
      ctx->sa->updates++;
      ctx->sa->used = (uint64_t)vbuf.bytesused;
      uwsgi_rwunlock(ctx->sa->lock);

      // re-enqueue buf
      if (xioctl(ctx->sa->fd, VIDIOC_QBUF, &vbuf) < 0) {
        uwsgi_error("ioctl() failed");
        return -1;
      }
    }
  }

  return 0;
}
