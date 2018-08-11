#pragma once

int xioctl(int fd, int ctl, void *arg);
void uwsgi_opt_set_8bit(char *opt, char *value, void *key);
void uwsgi_opt_set_resolution(char *opt, char *value, void *key);
