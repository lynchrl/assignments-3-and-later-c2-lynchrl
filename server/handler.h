#ifndef HANDLER_H
#define HANDLER_H

#include <pthread.h>
#include "aesdsocket.h"

void *handle_connection(void *arg);
int append_packet(const char *fname, const char *buf, size_t len);

#endif // HANDLER_H