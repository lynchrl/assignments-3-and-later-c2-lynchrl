#ifndef HANDLER_H
#define HANDLER_H

#include <pthread.h>
#include "aesdsocket.h"

#define SEEKTO "AESDCHAR_IOCSEEKTO:"

void *handle_connection(void *arg);
int append_packet(const char *fname, const char *buf, size_t len);
int write_to_client(int fd, int clfd);

#endif // HANDLER_H