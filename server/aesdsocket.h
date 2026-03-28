#ifndef AESDSOCKET_H
#define AESDSOCKET_H

#include <stddef.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "queue.h"

#define SERVER_PORT 9000
#define BUFFER_SIZE 1024

#ifdef USE_AESD_CHAR_DEVICE
#define FILENAME "/var/tmp/aesdsocketdata"
#else
#define FILENAME "/dev/aesdchar"
#endif

typedef struct conn_node
{
    // Thread ID and client address for this connection.
    pthread_t thread_id;
    struct sockaddr_in cli_addr;

    // Client socket file descriptor for this connection.
    int clfd;

#ifndef USE_AESD_CHAR_DEVICE
    // Mutex for output file access. Passed by the caller.
    pthread_mutex_t *file_mutex;
#endif

    // Indicates whether this connection has completed processing
    // and can be safely cleaned up.
    bool done;

    SLIST_ENTRY(conn_node)
    nodes;
} conn_node_t;

typedef SLIST_HEAD(conn_node_s, conn_node) conn_node_head_t;

typedef struct server_info
{
    // Singly-linked list head for tracking client handler threads.
    conn_node_head_t conn_node_head;

    // Shared mutex for output file synchronization.
    pthread_mutex_t file_mutex;
} server_info_t;

#endif