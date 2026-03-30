#include <syslog.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>

#include "handler.h"
#include "aesd_ioctl.h"

void *handle_connection(void *arg)
{
    conn_node_t *conn_info = (conn_node_t *)arg;
    char read_buffer[BUFFER_SIZE];
    ssize_t n;

    // Buffer to store data up to a newline character. We will read data in BUFFER_SIZE
    // chunks and write to file when a newline is encountered.
    char *data_buffer = malloc(BUFFER_SIZE);
    if (data_buffer == NULL)
    {
        perror("malloc for data_buffer");
        syslog(LOG_USER | LOG_ERR, "Error allocating buffer [%s]", strerror(errno));
        close(conn_info->clfd);
        conn_info->done = true;
        return NULL;
    }
    size_t data_len = 0;

    // Read data from client and write to file
    while ((n = recv(conn_info->clfd, read_buffer, BUFFER_SIZE - 1, 0)) > 0)
    {
        // Store data in a buffer and write to file when a newline is encountered. If we exceed the data buffer size,
        // we need to malloc a larger buffer and copy the existing data over.
        if (n < 0)
        {
            perror("Error reading from socket");
            syslog(LOG_USER | LOG_ERR, "Error reading from socket [%s]", strerror(errno));
            close(conn_info->clfd);
            conn_info->done = true;
            free(data_buffer);
            return NULL;
        }

        // Grow data_buffer if necessary.
        if (data_len + n >= BUFFER_SIZE)
        {
            // Double the current buffer size
            size_t new_size = (data_len + n) * 2;
            char *new_buffer = realloc(data_buffer, new_size);
            if (new_buffer == NULL)
            {
                perror("realloc for data_buffer");
                syslog(LOG_USER | LOG_ERR, "Error reallocating buffer [%s]", strerror(errno));
                close(conn_info->clfd);
                conn_info->done = true;
                return NULL;
            }
            data_buffer = new_buffer;
        }

        // Store n bytes from read_buffer into data_buffer, reallocing data_buffer
        // if necessary. We will write to file when we encounter a newline character
        // in read_buffer.
        memcpy(data_buffer + data_len, read_buffer, n);
        data_len += n;
    }
    syslog(LOG_USER | LOG_DEBUG, "Received %ld bytes from client", data_len);
    syslog(LOG_USER | LOG_DEBUG, "Data received from client: %.*s", (int)data_len, data_buffer);

    // Find location of newline characters in read_buffer and write data up to the
    // newline to file. Then write full file contents back to client.
    char *newline_ptr = NULL;
    newline_ptr = memchr(data_buffer, '\n', data_len);

    // If we encounter a newline character, write the data "packet" to file and reset the buffer.
    if (newline_ptr != NULL)
    {
#ifndef USE_AESD_CHAR_DEVICE
        pthread_mutex_lock(conn_info->file_mutex);
#endif
        int fd = open(FILENAME, O_RDWR | O_CREAT | O_APPEND, 0644);
        if (fd < 0)
        {
            perror("open");
            syslog(LOG_USER | LOG_ERR, "Error opening file (%s) [%s]", FILENAME, strerror(errno));
            close(conn_info->clfd);
            free(data_buffer);
            conn_info->done = true;
            return NULL;
        }
        // Append data to file
        if (append_packet(fd, data_buffer, (int)(newline_ptr - data_buffer) + 1) != 0)
        {
            syslog(LOG_USER | LOG_ERR, "Error appending packet to %s", FILENAME);
#ifndef USE_AESD_CHAR_DEVICE
            pthread_mutex_unlock(conn_info->file_mutex);
#endif
            close(conn_info->clfd);
            conn_info->done = true;
            return NULL;
        }
        if (close(fd) < 0)
        {
            syslog(LOG_USER | LOG_ERR, "Error closing (write) file (%s) [%s]", FILENAME, strerror(errno));
        }

        // Write full file contents back to client.
        int read_fd = open(FILENAME, O_RDONLY);
        if (read_fd < 0)
        {
            perror("open");
            syslog(LOG_USER | LOG_ERR, "Error opening file (%s) [%s]", FILENAME, strerror(errno));
            close(conn_info->clfd);
            free(data_buffer);
            conn_info->done = true;
            return NULL;
        }
        syslog(LOG_USER | LOG_DEBUG, "Sending file contents back to client");
        while ((n = read(read_fd, read_buffer, BUFFER_SIZE)) > 0)
        {
            if (n < 0)
            {
                perror("read");
                syslog(LOG_USER | LOG_ERR, "Error reading file (%s) [%s]", FILENAME, strerror(errno));
                break;
            }
            // printf("Sending <%s> to client\n", read_buffer);
            syslog(LOG_USER | LOG_DEBUG, "Sending %ld bytes back to client", n);
            syslog(LOG_USER | LOG_DEBUG, "Data being sent back to client: %.*s", (int)n, read_buffer);
            ssize_t sent = send(conn_info->clfd, read_buffer, n, 0);
            if (sent != n)
            {
                syslog(LOG_USER | LOG_ERR, "Read (%ld) and send (%ld) mismatch", n, sent);
                break;
            }
            syslog(LOG_USER | LOG_DEBUG, "Sent %ld bytes back to client", sent);
        }
        if (close(read_fd) < 0)
        {
            syslog(LOG_USER | LOG_ERR, "Error closing (read) file (%s) [%s]", FILENAME, strerror(errno));
        }
        syslog(LOG_USER | LOG_DEBUG, "Finished sending file contents back to client. Closed file.");
#ifndef USE_AESD_CHAR_DEVICE
        pthread_mutex_unlock(conn_info->file_mutex);
#endif
    }

    free(data_buffer);
    if (close(conn_info->clfd) < 0)
    {
        syslog(LOG_USER | LOG_ERR, "Error closing client socket [%s]", strerror(errno));
    }
    syslog(LOG_USER | LOG_DEBUG, "Closed connection from %s", inet_ntoa(conn_info->cli_addr.sin_addr));
    conn_info->done = true;
    return NULL;
}

int append_packet(int fd, const char *buf, size_t len)
{
    ssize_t res = write(fd, buf, len);
    if (res == -1)
    {
        perror("Error writing to file (filename: " FILENAME ")");
        syslog(LOG_USER | LOG_ERR, "Error writing to file [%s]: %s", strerror(errno), FILENAME);
        close(fd);
        return -1;
    }
    return 0;
}
