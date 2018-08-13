#include "control.h"
#include "util.h"
#include "uwsgiwrap.h"
#include "v4l.h"
#include <linux/videodev2.h>
#include <string.h>

static int is_v4l_control(capture_context *ctx, unsigned int id) {
  struct v4l2_queryctrl queryctrl;
  memset(&queryctrl, 0, sizeof(queryctrl));
  queryctrl.id = id;

  int ret = xioctl(ctx->sa->fd, VIDIOC_QUERYCTRL, &queryctrl);
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

int v4l_get_control(capture_context *ctx, unsigned int id) {
  int ret = is_v4l_control(ctx, id);
  if (ret < 0) {
    return ret;
  }

  struct v4l2_control control_s;
  memset(&control_s, 0, sizeof(control_s));
  control_s.id = id;
  ret = xioctl(ctx->sa->fd, VIDIOC_G_CTRL, &control_s);
  if (ret < 0) {
    uwsgi_error("getcontrol ioctl() failed");
    return ret;
  }

  return control_s.value;
}

int v4l_set_control(capture_context *ctx, unsigned int id, int value) {
  int ret = is_v4l_control(ctx, id);
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
      int ret = xioctl(ctx->sa->fd, VIDIOC_S_CTRL, &control_s);
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
    ret = xioctl(ctx->sa->fd, VIDIOC_S_EXT_CTRLS, &ext_ctrls);
    if (ret) {
      uwsgi_log("control id: 0x%08x failed to set value (error %i)\n",
                ext_ctrl.id, ret);
      return ret;
    }
    return 0;
  }
}

int v4l_reset_control(capture_context *ctx, unsigned int id) {
  int ret = is_v4l_control(ctx, id);
  if (ret < 0) {
    return ret;
  }

  struct v4l2_queryctrl queryctrl;
  memset(&queryctrl, 0, sizeof(queryctrl));
  queryctrl.id = id;

  ret = xioctl(ctx->sa->fd, VIDIOC_QUERYCTRL, &queryctrl);
  if (ret < 0) {
    uwsgi_error("querycontrol ioctl() failed");
    return -1;
  }

  struct v4l2_control control_s;
  memset(&control_s, 0, sizeof(control_s));
  control_s.value = queryctrl.default_value;
  control_s.id = id;
  ret = xioctl(ctx->sa->fd, VIDIOC_S_CTRL, &control_s);
  if (ret < 0) {
    uwsgi_error("setcontrol ioctl() failed");
    return -1;
  }

  return 0;
}

static void v4l_add_control(capture_context *ctx, struct v4l2_queryctrl *ctrl) {
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
      if (xioctl(ctx->sa->fd, VIDIOC_QUERYMENU, &qm) == 0) {
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
    ret = xioctl(ctx->sa->fd, VIDIOC_G_CTRL, &c);
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
    ret = xioctl(ctx->sa->fd, VIDIOC_G_EXT_CTRLS, &ext_ctrls);
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

void v4l_enumerate_controls(capture_context *ctx) {
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
  if (ioctl(ctx->sa->fd, VIDIOC_QUERYCTRL, &ctrl) == 0) {
    do {
      v4l_add_control(ctx, &ctrl);
      ctrl.id |= V4L2_CTRL_FLAG_NEXT_CTRL;
    } while (ioctl(ctx->sa->fd, VIDIOC_QUERYCTRL, &ctrl) == 0);
  } else
#endif
  {
    // fall back on the standard API

    // check all the standard controls
    for (int i = V4L2_CID_BASE; i < V4L2_CID_LASTP1; i++) {
      ctrl.id = i;
      if (ioctl(ctx->sa->fd, VIDIOC_QUERYCTRL, &ctrl) == 0) {
        v4l_add_control(ctx, &ctrl);
      }
    }

    // check any custom controls
    for (int i = V4L2_CID_PRIVATE_BASE;; i++) {
      ctrl.id = i;
      ret = ioctl(ctx->sa->fd, VIDIOC_QUERYCTRL, &ctrl);
      if (ret < 0) {
        break;
      }
      v4l_add_control(ctx, &ctrl);
    }
  }
}

#define V4L_OPT_SET(vid, var, desc)                                            \
  if (ctx->control_options.var.set) {                                          \
    ret = v4l_set_control(ctx, vid, ctx->control_options.var.value);           \
    if (ret == 0) {                                                            \
      uwsgi_log("set " desc " of %s to %d\n", ctx->name,                       \
                ctx->control_options.var.value);                               \
    } else {                                                                   \
      uwsgi_log("Failed to set " desc "\n");                                   \
    }                                                                          \
  }

int v4l_setup_controls(capture_context *ctx) {
  int ret;

  V4L_OPT_SET(V4L2_CID_SHARPNESS, sh, "sharpness")
  V4L_OPT_SET(V4L2_CID_CONTRAST, co, "contrast")
  V4L_OPT_SET(V4L2_CID_SATURATION, sa, "saturation")
  V4L_OPT_SET(V4L2_CID_BACKLIGHT_COMPENSATION, bk, "backlight compensation")
  V4L_OPT_SET(V4L2_CID_ROTATE, rot, "rotation")
  V4L_OPT_SET(V4L2_CID_HFLIP, hf, "hflip")
  V4L_OPT_SET(V4L2_CID_VFLIP, vf, "vflip")
  V4L_OPT_SET(V4L2_CID_VFLIP, pl, "power line filter")

  /*if (settings->br_set) {
    V4L_OPT_SET(V4L2_CID_AUTOBRIGHTNESS, br_auto, "auto brightness mode")

    if (settings->br_auto == 0) {
      V4L_OPT_SET(V4L2_CID_BRIGHTNESS, br, "brightness")
    }
  }

  if (settings->wb_set) {
    V4L_OPT_SET(V4L2_CID_AUTO_WHITE_BALANCE, wb_auto, "auto white balance mode")

    if (settings->wb_auto == 0) {
      V4L_OPT_SET(V4L2_CID_WHITE_BALANCE_TEMPERATURE, wb,
                  "white balance temperature")
    }
  }

  if (settings->ex_set) {
    V4L_OPT_SET(V4L2_CID_EXPOSURE_AUTO, ex_auto, "exposure mode")
    if (settings->ex_auto == V4L2_EXPOSURE_MANUAL) {
      V4L_OPT_SET(V4L2_CID_EXPOSURE_ABSOLUTE, ex, "absolute exposure")
    }
  }

  if (settings->gain_set) {
    V4L_OPT_SET(V4L2_CID_AUTOGAIN, gain_auto, "auto gain mode")

    if (settings->gain_auto == 0) {
      V4L_OPT_SET(V4L2_CID_GAIN, gain, "gain")
    }
  }

  if (settings->cagc_set) {
    V4L_OPT_SET(V4L2_CID_AUTO_WHITE_BALANCE, cagc_auto, "chroma gain mode")

    if (settings->cagc_auto == 0) {
      V4L_OPT_SET(V4L2_CID_WHITE_BALANCE_TEMPERATURE, cagc, "chroma gain")
    }
  }

  if (settings->cb_set) {
    V4L_OPT_SET(V4L2_CID_HUE_AUTO, cb_auto, "color balance mode")

    if (settings->cb_auto == 0) {
      V4L_OPT_SET(V4L2_CID_HUE, cagc, "color balance")
    }
  }*/

  return 0;
}
