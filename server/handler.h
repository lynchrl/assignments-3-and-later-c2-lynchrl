#ifndef HANDLER_H
#define HANDLER_H

#include <pthread.h>
#include "aesdsocket.h"

#define SEEKTO "AESDCHAR_IOCSEEKTO:"

void *handle_connection(void *arg);
int append_packet(int fd, const char *buf, size_t len);

#endif // HANDLER_H