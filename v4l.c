#include <inttypes.h>
#include <linux/videodev2.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <uwsgi.h>

extern struct uwsgi_server uwsgi;
struct uwsgi_sharedarea *sa;

static struct {
  uint16_t quality, fps;
  bool hf;
  char *name;
  char *path;
  uint16_t resolution[2];
} ctx = {.quality = 80,
         .fps = 255,
         .hf = 0,
         .name = "Unknown",
         .path = "/dev/video0",
         .resolution = {640, 480}};

#define IOCTL_RETRY 4

// ioctl with a number of retries in the case of I/O failure
int xioctl(int fd, int ctl, void *arg) {
  int ret = 0;
  int tries = IOCTL_RETRY;
  do {
    ret = ioctl(fd, ctl, arg);
  } while (ret && tries-- &&
           ((errno == EINTR) || (errno == EAGAIN) || (errno == ETIMEDOUT)));

  if (ret && (tries <= 0)) {
    uwsgi_debug("ioctl (%i) retried %i times - giving up: %s\n", ctl,
                IOCTL_RETRY, strerror(errno));
  }

  return (ret);
}

void uwsgi_opt_set_8bit(char *opt, char *value, void *key) {
  uint8_t *ptr = (uint8_t *)key;

  if (value) {
    unsigned long n = strtoul(value, NULL, 10);
    if (n > 255)
      n = 255;
    *ptr = n;
  } else {
    *ptr = 1;
  }
}

static void uwsgi_opt_set_resolution(char *opt, char *value, void *key) {
  int *res = (int *)key;
  if (sscanf(optarg, SCNu16 "x" SCNu16, &res[0], &res[1]) != 2) {
    uwsgi_log("Invalid resolution '%s' specified", optarg);
    exit(EXIT_FAILURE);
  }
}

static struct uwsgi_option capture_options[] = {
    {"v4l-device", required_argument, 0,
     "capture from the specified v4l device", uwsgi_opt_set_str, &ctx.path, 0},
    {"resolution", required_argument, 0, "resolution of the captured video",
     uwsgi_opt_set_resolution, ctx.resolution, 0},
    {"quality", required_argument, 0, "JPEG quality (0-100) of each frame",
     uwsgi_opt_set_8bit, &ctx.quality, 0},
    {"fps", required_argument, 0, "number of frames to generate per second",
     uwsgi_opt_set_8bit, &ctx.fps, 0},
    {"hf", no_argument, 0, "flip the video horizontally", uwsgi_opt_true,
     &ctx.hf, 0},
    {NULL, 0, 0, NULL, NULL, NULL, 0},
};

static int captureinit() {
  if (ctx.quality > 100) {
    ctx.quality = 100;
  }

  int fd = open(ctx.path, O_RDWR | O_NONBLOCK);
  if (fd == -1) {
    uwsgi_log("Error opening V4L interface");
    exit(EXIT_FAILURE);
  }

  struct v4l2_capability cap;
  memset(&cap, 0, sizeof(cap));
  if (xioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
    uwsgi_log("Error opening device %s: unable to query device.", ctx.path);
    exit(EXIT_FAILURE);
  }

  if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
    uwsgi_log("Error opening device %s: video capture not supported.",
              ctx.path);
  }

  if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
    uwsgi_log("Device %s does not support streaming I/O", ctx.path);
  }

  struct v4l2_format fmt;
  memset(&fmt, 0, sizeof(fmt));
  fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  fmt.fmt.pix.width = ctx.resolution[0];
  fmt.fmt.pix.height = ctx.resolution[1];
  fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
  fmt.fmt.pix.field = V4L2_FIELD_ANY;
  if (xioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
    uwsgi_log("Unable to set pixel format MJPEG @ resolution %dx%d",
              ctx.resolution[0], ctx.resolution[1]);
    exit(EXIT_FAILURE);
  }

  if (fmt.fmt.pix.width != ctx.resolution[0] ||
      fmt.fmt.pix.height != ctx.resolution[1]) {
    uwsgi_log("Specified resolution is unavailable, using %dx%d instead",
              fmt.fmt.pix.width, fmt.fmt.pix.height);
    ctx.resolution[0] = fmt.fmt.pix.width;
    ctx.resolution[1] = fmt.fmt.pix.height;
  }

  if (fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_MJPEG &&
      fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_JPEG) {
    uwsgi_log("Device %s does not support native MJPEG encoding", ctx.path);
    exit(EXIT_FAILURE);
  }

  struct v4l2_streamparm setfps;
  memset(&setfps, 0, sizeof(setfps));
  setfps.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

  if (xioctl(fd, VIDIOC_G_PARM, &setfps) < 0 ||
      !(setfps.parm.capture.capability & V4L2_CAP_TIMEPERFRAME)) {
    uwsgi_log("Can't change FPS for device %s", ctx.path);
  } else {
    memset(&setfps, 0, sizeof(setfps));
    setfps.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    setfps.parm.capture.timeperframe.numerator = 1;
    setfps.parm.capture.timeperframe.denominator = ctx.fps;

    if (xioctl(fd, VIDIOC_S_PARM, &setfps)) {
      uwsgi_log("Can't set FPS of device %s", ctx.path);
    }
  }

  struct v4l2_requestbuffers rb;
  rb.count = 1;
  rb.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  rb.memory = V4L2_MEMORY_MMAP;

  if (xioctl(fd, VIDIOC_REQBUFS, &rb) < 0) {
    uwsgi_log("Unable to allocate/mmap buffer for device %s", ctx.path);
    exit(EXIT_FAILURE);
  }

  struct v4l2_buffer vbuf;
  memset(&vbuf, 0, sizeof(vbuf));
  vbuf.index = 0;
  vbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  vbuf.memory = V4L2_MEMORY_MMAP;
  if (xioctl(fd, VIDIOC_QUERYBUF, &vbuf) < 0) {
    uwsgi_log("Unable to query mmap'ed buffer for device %s", ctx.path);
    exit(EXIT_FAILURE);
  }

  uint64_t area_len = vbuf.length;
  char *area = mmap(NULL, vbuf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
                    vbuf.m.offset);

  sa = uwsgi_sharedarea_init_ptr(area, area_len);
  sa->fd = fd;
  sa->honour_used = 1;

  memset(&vbuf, 0, sizeof(vbuf));
  vbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  vbuf.memory = V4L2_MEMORY_MMAP;
  vbuf.index = 0;

  if (xioctl(fd, VIDIOC_QBUF, &vbuf) < 0) {
    uwsgi_log("Unable to queue mmap'ed buffer for device %s", ctx.path);
    exit(EXIT_FAILURE);
  }

  struct v4l2_input in_struct;
  memset(&in_struct, 0, sizeof(in_struct));
  in_struct.index = 0;
  if (!xioctl(fd, VIDIOC_ENUMINPUT, &in_struct)) {
    ctx.name = strdup(in_struct.name);
  }

  int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (xioctl(fd, VIDIOC_STREAMON, &type) < 0) {
    uwsgi_log("Unable to start capture stream for device %s", ctx.path);
    exit(EXIT_FAILURE);
  }

  uwsgi_log("%s started streaming frames to sharedarea %d\n", ctx.path, sa->id);

  return 0;
}

void captureloop() {

  while (1) {
    struct pollfd p;
    p.events = POLLIN;
    p.fd = sa->fd;
    int ret = poll(&p, 1, -1);
    if (ret < 0) {
      uwsgi_log("poll()");
      exit(EXIT_FAILURE);
    }
    // uwsgi_log("ret = %d\n", ret);
    struct v4l2_buffer vbuf;
    memset(&vbuf, 0, sizeof(struct v4l2_buffer));
    vbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    vbuf.memory = V4L2_MEMORY_MMAP;

    // dequeue buf
    uwsgi_wlock(sa->lock);
    if (xioctl(sa->fd, VIDIOC_DQBUF, &vbuf) < 0) {
      uwsgi_log("ioctl()");
      exit(EXIT_FAILURE);
    }
    sa->updates++;
    sa->used = (uint64_t)vbuf.bytesused;
    uwsgi_rwunlock(sa->lock);

    // re-enqueue buf
    if (xioctl(sa->fd, VIDIOC_QBUF, &vbuf) < 0) {
      uwsgi_log("ioctl()");
      exit(EXIT_FAILURE);
    }
  }
}

struct uwsgi_plugin capture_plugin = {
    .name = "capture",
    .options = capture_options,
    .init = captureinit,
};
