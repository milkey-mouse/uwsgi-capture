#include "control.h"
#include "util.h"
#include "uwsgiwrap.h"
#include "v4l.h"
#include <linux/videodev2.h>
#include <string.h>

static int is_v4l_control(int fd, unsigned int id) {
  struct v4l2_queryctrl queryctrl;
  memset(&queryctrl, 0, sizeof(queryctrl));
  queryctrl.id = id;

  int ret = xioctl(fd, VIDIOC_QUERYCTRL, &queryctrl);
  if (ret < 0) {
    uwsgi_error("querycontrol ioctl() failed");
    return ret;
  }

  if (queryctrl.flags & V4L2_CTRL_FLAG_DISABLED) {
    uwsgi_log("control %s disabled", queryctrl.name);
    return -1;
  }

  if (queryctrl.type & V4L2_CTRL_TYPE_BOOLEAN) {
    return 1;
  }

  if (queryctrl.type & V4L2_CTRL_TYPE_INTEGER) {
    return 0;
  }

  uwsgi_log("control %s unsupported\n", queryctrl.name);
  return -1;
}

int v4l_get_control(int fd, unsigned int id) {
  int ret = is_v4l_control(fd, id);
  if (ret < 0) {
    return ret;
  }

  struct v4l2_control control_s;
  memset(&control_s, 0, sizeof(control_s));
  control_s.id = id;
  ret = xioctl(fd, VIDIOC_G_CTRL, &control_s);
  if (ret < 0) {
    uwsgi_error("getcontrol ioctl() failed");
    return ret;
  }

  return control_s.value;
}

int v4l_set_control(int fd, unsigned int id, int value, capture_context *ctx) {
  int ret = is_v4l_control(fd, id);
  if (ret < 0) {
    uwsgi_log("tried to set invalid control id 0x%08x\n", id);
    return ret;
  }

  v4l_control_meta *ctrl = NULL;
  for (int i = 0; i < ctx->control_count; i++) {
    if (ctx->controls[i].ctrl.id == id) {
      ctrl = &ctx->controls[i];
      break;
    }
  }

  if (ctrl == NULL) {
    uwsgi_log("control 0x%08x is not in the list", id);
    return -1;
  }

  if (ctrl->class_id == V4L2_CTRL_CLASS_USER) {
    int min = ctrl->ctrl.minimum;
    int max = ctrl->ctrl.maximum;

    if ((value >= min) && (value <= max)) {
      struct v4l2_control control_s;
      memset(&control_s, 0, sizeof(control_s));
      control_s.id = id;
      control_s.value = value;
      int ret = xioctl(fd, VIDIOC_S_CTRL, &control_s);
      if (ret < 0) {
        uwsgi_error("ioctl() failed");
        return ret;
      }
      ctrl->value = value;
    } else {
      uwsgi_log("value %d for control 0x%08x out of range %d-%d\n", value, id,
                min, max);
      return -1;
    }
    return 0;
  } else {
    // not a user class control
    struct v4l2_ext_controls ext_ctrls;
    memset(&ext_ctrls, 0, sizeof(ext_ctrls));
    struct v4l2_ext_control ext_ctrl;
    memset(&ext_ctrl, 0, sizeof(ext_ctrl));
    ext_ctrl.id = ctrl->ctrl.id;

    switch (ctrl->ctrl.type) {
#ifdef V4L2_CTRL_TYPE_STRING
    case V4L2_CTRL_TYPE_STRING:
      // string gets set on VIDIOC_G_EXT_CTRLS
      // add the maximum size to value
      ext_ctrl.size = value;
      // STRING extended controls are currently broken
      // ext_ctrl.string = control->string; // FIXMEE
      break;
#endif
    case V4L2_CTRL_TYPE_INTEGER64:
      ext_ctrl.value64 = value;
      break;
    default:
      ext_ctrl.value = value;
      break;
    }

    ext_ctrls.count = 1;
    ext_ctrls.controls = &ext_ctrl;
    ret = xioctl(fd, VIDIOC_S_EXT_CTRLS, &ext_ctrls);
    if (ret) {
      uwsgi_log("control id: 0x%08x failed to set value (error %i)\n",
                ext_ctrl.id, ret);
      return ret;
    }
    return 0;
  }
}

int v4l_reset_control(int fd, unsigned int id) {
  int ret = is_v4l_control(fd, id);
  if (ret < 0) {
    return ret;
  }

  struct v4l2_queryctrl queryctrl;
  memset(&queryctrl, 0, sizeof(queryctrl));
  queryctrl.id = id;

  ret = xioctl(fd, VIDIOC_QUERYCTRL, &queryctrl);
  if (ret < 0) {
    uwsgi_error("querycontrol ioctl() failed");
    return -1;
  }

  struct v4l2_control control_s;
  memset(&control_s, 0, sizeof(control_s));
  control_s.value = queryctrl.default_value;
  control_s.id = id;
  ret = xioctl(fd, VIDIOC_S_CTRL, &control_s);
  if (ret < 0) {
    uwsgi_error("setcontrol ioctl() failed");
    return -1;
  }

  return 0;
}

static void v4l_add_control(int fd, struct v4l2_queryctrl *ctrl,
                            capture_context *ctx) {
  struct v4l2_control c;
  memset(&c, 0, sizeof(c));
  c.id = ctrl->id;

  ctx->controls = (v4l_control_meta *)realloc(
      ctx->controls, (ctx->control_count + 1) * sizeof(ctx->controls[0]));
  if (ctx->controls == NULL) {
    uwsgi_fatal_error("realloc() failed");
  }

  ctx->controls[ctx->control_count].ctrl = *ctrl;
  ctx->controls[ctx->control_count].value = c.value;
  /*if (ctrl->type == V4L2_CTRL_TYPE_MENU) {
    ctx->controls[ctx->control_count].menuitems = (struct v4l2_querymenu
  *)malloc( (ctrl->maximum + 1) * sizeof(struct v4l2_querymenu)); int i; for (i
  = ctrl->minimum; i <= ctrl->maximum; i++) { struct v4l2_querymenu qm;
      memset(&qm, 0, sizeof(struct v4l2_querymenu));
      qm.id = ctrl->id;
      qm.index = i;
      if (xioctl(fd, VIDIOC_QUERYMENU, &qm) == 0) {
        memcpy(&ctx->in_parameters[ctx->control_count].menuitems[i], &qm,
               sizeof(struct v4l2_querymenu));
        DBG("Menu item %d: %s\n", qm.index, qm.name);
      } else {
        DBG("Unable to get menu item for %s, index=%d\n", ctrl->name, qm.index);
      }
    }
  } else {*/
  ctx->controls[ctx->control_count].menuitems = NULL;
  /*}*/

  ctx->controls[ctx->control_count].value = 0;
  ctx->controls[ctx->control_count].class_id = (ctrl->id & 0xFFFF0000);
#ifndef V4L2_CTRL_FLAG_NEXT_CTRL
  ctx->controls[ctx->control_count].class_id = V4L2_CTRL_CLASS_USER;
#endif

  int ret = -1;
  if (ctx->controls[ctx->control_count].class_id == V4L2_CTRL_CLASS_USER) {
    ret = xioctl(fd, VIDIOC_G_CTRL, &c);
    if (ret < 0) {
      uwsgi_log("unable to get the value of control %s", ctrl->name);
    } else {
      ctx->controls[ctx->control_count].value = c.value;
    }
  } else {
    struct v4l2_ext_controls ext_ctrls;
    memset(&ext_ctrls, 0, sizeof(ext_ctrls));
    struct v4l2_ext_control ext_ctrl;
    memset(&ext_ctrl, 0, sizeof(ext_ctrl));
    ext_ctrl.id = ctrl->id;
#ifdef V4L2_CTRL_TYPE_STRING
    ext_ctrl.size = 0;
    if (ctrl.type == V4L2_CTRL_TYPE_STRING) {
      ext_ctrl.size = ctrl->maximum + 1;
      // FIXMEEEEext_ctrl.string = control->string;
    }
#endif
    ext_ctrls.count = 1;
    ext_ctrls.controls = &ext_ctrl;
    ret = xioctl(fd, VIDIOC_G_EXT_CTRLS, &ext_ctrls);
    if (ret) {
      switch (ext_ctrl.id) {
      case V4L2_CID_PAN_RESET:
        ctx->controls[ctx->control_count].value = 1;
        break;
      case V4L2_CID_TILT_RESET:
        ctx->controls[ctx->control_count].value = 1;
        break;
      /*case V4L2_CID_PANTILT_RESET_LOGITECH:
        ctx->controls[ctx->control_count].value = 3;
        DBG("Setting the PAN/TILT reset value to 3\n");
        break;*/
      default:
        uwsgi_log("control id: 0x%08x failed to get value (error %i)\n",
                  ext_ctrl.id, ret);
      }
    } else {
      switch (ctrl->type) {
#ifdef V4L2_CTRL_TYPE_STRING
      case V4L2_CTRL_TYPE_STRING:
        // string gets set on VIDIOC_G_EXT_CTRLS
        // add the maximum size to value
        ctx->controls[ctx->control_count].value = ext_ctrl.size;
        break;
#endif
      case V4L2_CTRL_TYPE_INTEGER64:
        ctx->controls[ctx->control_count].value = ext_ctrl.value64;
        break;
      default:
        ctx->controls[ctx->control_count].value = ext_ctrl.value;
        break;
      }
    }
  }

  ctx->control_count++;
}

void v4l_enumerate_controls(int fd, capture_context *ctx) {
  struct v4l2_queryctrl ctrl;
  memset(&ctrl, 0, sizeof(ctrl));

  ctx->control_count = 0;
  free(ctx->controls);
  ctx->controls = NULL;
  // try the extended control API first
#ifdef V4L2_CTRL_FLAG_NEXT_CTRL
  // note: use simple ioctl or v4l2_ioctl instead of the xioctl
  ctrl.id = V4L2_CTRL_FLAG_NEXT_CTRL;
  int ret = -1;
  if (ioctl(fd, VIDIOC_QUERYCTRL, &ctrl) == 0) {
    do {
      v4l_add_control(fd, &ctrl, ctx);
      ctrl.id |= V4L2_CTRL_FLAG_NEXT_CTRL;
    } while (ioctl(fd, VIDIOC_QUERYCTRL, &ctrl) == 0);
  } else
#endif
  {
    // fall back on the standard API

    // check all the standard controls
    for (int i = V4L2_CID_BASE; i < V4L2_CID_LASTP1; i++) {
      ctrl.id = i;
      if (ioctl(fd, VIDIOC_QUERYCTRL, &ctrl) == 0) {
        v4l_add_control(fd, &ctrl, ctx);
      }
    }

    // check any custom controls
    for (int i = V4L2_CID_PRIVATE_BASE;; i++) {
      ctrl.id = i;
      ret = ioctl(fd, VIDIOC_QUERYCTRL, &ctrl);
      if (ret < 0) {
        break;
      }
      v4l_add_control(fd, &ctrl, ctx);
    }
  }
}
