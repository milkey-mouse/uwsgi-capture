#pragma once

#include <inttypes.h>
#ifndef SCNu16
#define SCNu16 "u"
#endif

#define IOCTL_RETRY 4
int xioctl(int fd, int ctl, void *arg);

#define OPT_AUTO -1
void uwsgi_opt_set_8bit(char *opt, char *value, void *key);
void uwsgi_opt_set_resolution(char *opt, char *value, void *key);
void uwsgi_opt_set_str_and_add_mule(char *opt, char *value, void *key);
void uwsgi_opt_set_ctrl_int(char *opt, char *value, void *key);
void uwsgi_opt_set_ctrl_int_or_auto(char *opt, char *value, void *key);
void uwsgi_opt_set_ctrl_bool(char *opt, char *value, void *key);

void uwsgi_opt_set_ctrl_tvnorm(char *opt, char *value, void *key);
void uwsgi_opt_set_ctrl_ex(char *opt, char *value, void *key);
void uwsgi_opt_set_ctrl_pl(char *opt, char *value, void *key);
