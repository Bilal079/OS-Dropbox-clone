#ifndef UTIL_H
#define UTIL_H

#include <stddef.h>
#include <stdint.h>

int set_nonblocking(int fd);
int read_line(int fd, char *buf, size_t maxlen); // reads until \n, strips
int read_n(int fd, void *buf, size_t n);
int write_n(int fd, const void *buf, size_t n);
int send_fmt(int fd, const char *fmt, ...);
uint64_t now_millis(void);

#endif


