#include "util.h"
#include "uwsgiwrap.h"
#include "v4l.h"
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
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

void uwsgi_opt_set_ctrl_int(char *opt, char *value, void *key) {
  control_option *copt = (control_option *)key;
  copt->set = true;
  uwsgi_opt_set_int(opt, value, &copt->value);
}

void uwsgi_opt_set_ctrl_int_or_auto(char *opt, char *value, void *key) {
  control_option *copt = (control_option *)key;
  copt->set = true;
  if (strcasecmp(value, "auto") == 0) {
    copt->value = OPT_AUTO;
  } else {
    uwsgi_opt_set_int(opt, value, &copt->value);
    if (copt->value == OPT_AUTO) {
      uwsgi_log("tried to use value reserved for OPT_AUTO");
      copt->set = false;
    }
  }
}

void uwsgi_opt_set_ctrl_bool(char *opt, char *value, void *key) {
  control_option *copt = (control_option *)key;
  copt->set = true;
  uwsgi_opt_true(opt, value, &copt->value);
}

void uwsgi_opt_set_ctrl_tvnorm(char *opt, char *value, void *key) {
  v4l2_std_id *tvnorm = (v4l2_std_id *)key;

  if (strcasecmp(value, "pal") == 0) {
    *tvnorm = V4L2_STD_PAL;
  } else if (strcasecmp(value, "ntsc") == 0) {
    *tvnorm = V4L2_STD_NTSC;
  } else if (strcasecmp(value, "secam") == 0) {
    *tvnorm = V4L2_STD_SECAM;
  } else {
    *tvnorm = V4L2_STD_UNKNOWN;
  }
}

void uwsgi_opt_set_ctrl_ex(char *opt, char *value, void *key) {
  control_option_auto *ex = (control_option_auto *)key;
  ex->set = true;

  if (strcasecmp(value, "auto") == 0) {
    ex->value = V4L2_EXPOSURE_AUTO;
  } else if (strcasecmp(value, "shutter-priority") == 0) {
    ex->value = V4L2_EXPOSURE_SHUTTER_PRIORITY;
  } else if (strcasecmp(value, "aperture-priority") == 0) {
    ex->value = V4L2_EXPOSURE_APERTURE_PRIORITY;
  } else {
    ex->value = V4L2_EXPOSURE_MANUAL;
    uwsgi_opt_set_int(opt, value, &ex->manual_value);
  }
}

void uwsgi_opt_set_ctrl_pl(char *opt, char *value, void *key) {
  control_option_auto *ex = (control_option_auto *)key;
  ex->set = true;

  if (strcasecmp(value, "disabled") == 0) {
    ex->value = V4L2_CID_POWER_LINE_FREQUENCY_DISABLED;
  } else if (strcasecmp(value, "50hz") == 0) {
    ex->value = V4L2_CID_POWER_LINE_FREQUENCY_50HZ;
  } else if (strcasecmp(value, "60hz") == 0) {
    ex->value = V4L2_CID_POWER_LINE_FREQUENCY_60HZ;
  } else if (strcasecmp(value, "auto") == 0) {
    ex->value = V4L2_CID_POWER_LINE_FREQUENCY_AUTO;
  } else {
    ex->set = false;
  }
}
