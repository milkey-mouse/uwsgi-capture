#include "util.h"
#include "uwsgiwrap.h"
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

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

void uwsgi_opt_set_resolution(char *opt, char *value, void *key) {
  uint16_t *res = (uint16_t *)key;
  if (sscanf(optarg, "%" SCNu16 "x%" SCNu16, &res[0], &res[1]) != 2) {
    uwsgi_log("Invalid resolution '%s' specified\n", optarg);
    exit(EXIT_FAILURE);
  }
}

void uwsgi_opt_set_str_and_add_mule(char *opt, char *value, void *key) {
  uwsgi_opt_set_str(opt, value, key);
  uwsgi_opt_add_mule(NULL, "capture_loop()", NULL);
}
