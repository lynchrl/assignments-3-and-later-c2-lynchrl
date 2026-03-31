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

    int fd = open(FILENAME, O_RDONLY);
    if (fd < 0)
    {
        perror("open");
        syslog(LOG_USER | LOG_ERR, "Error opening file (%s) [%s]", FILENAME, strerror(errno));
        close(conn_info->clfd);
        free(data_buffer);
        conn_info->done = true;
        return NULL;
    }

    // Read data from client and write to file
    while ((n = recv(conn_info->clfd, read_buffer, BUFFER_SIZE - 1, 0)) > 0)
    {
        // Grow data_buffer if necessary.
        if (data_len + n >= BUFFER_SIZE)
        {
            size_t new_size = 2 * (data_len + n);
            char *new_buffer = realloc(data_buffer, new_size);
            if (new_buffer == NULL)
            {
                perror("realloc for data_buffer");
                syslog(LOG_USER | LOG_ERR, "Error reallocating buffer [%s]", strerror(errno));
                break; // Break out of the loop to close the connection
            }
            data_buffer = new_buffer;
        }
        memcpy(data_buffer + data_len, read_buffer, n);
        data_len += n;
        data_buffer[data_len] = '\0'; // Null-terminate for safety in string operations

        if (strncmp(data_buffer, SEEKTO, strlen(SEEKTO)) == 0)
        {
            // Handle seek command
            char *seek_args = data_buffer + strlen(SEEKTO);
            int write_cmd, write_cmd_offset;
            if (sscanf(seek_args, "%d,%d", &write_cmd, &write_cmd_offset) == 2)
            {
                syslog(LOG_USER | LOG_DEBUG, "Received seek command: write_cmd=%d, write_cmd_offset=%d", write_cmd, write_cmd_offset);
                struct aesd_seekto seekto = {
                    .write_cmd = (uint32_t)write_cmd,
                    .write_cmd_offset = (uint32_t)write_cmd_offset};
                int ioctl_result = ioctl(fd, AESDCHAR_IOCSEEKTO, &seekto);
                if (ioctl_result != 0)
                {
                    perror("ioctl");
                    syslog(LOG_USER | LOG_ERR, "ioctl failed with error: %d", ioctl_result);
                    break;
                }
            }
            else
            {
                syslog(LOG_USER | LOG_ERR, "Invalid seek command format: %s", seek_args);
            }
            // Reset the data buffer after processing the command
            data_len = 0;
            free(data_buffer);
            data_buffer = malloc(BUFFER_SIZE);
            if (data_buffer == NULL)
            {
                perror("malloc new data_buffer after seek command");
                syslog(LOG_USER | LOG_ERR, "Error allocating buffer [%s]", strerror(errno));
                break;
            }
            continue;
        }

        // If we encounter a newline character, write the data "packet" to file and reset the buffer.
        if (memchr(data_buffer, '\n', data_len) != NULL)
        {
            // Append data to file
#ifndef USE_AESD_CHAR_DEVICE
            pthread_mutex_lock(conn_info->file_mutex);
#endif
            if (append_packet(FILENAME, data_buffer, data_len) != 0)
            {
                syslog(LOG_USER | LOG_ERR, "Error appending packet to %s", FILENAME);
#ifndef USE_AESD_CHAR_DEVICE
                pthread_mutex_unlock(conn_info->file_mutex);
#endif
                break;
            }
#ifndef USE_AESD_CHAR_DEVICE
            pthread_mutex_unlock(conn_info->file_mutex);
#endif
            // syslog(LOG_USER | LOG_DEBUG, "Wrote <%s> to file <%s>", data_buffer, FILENAME);
            // Reset data buffer for next line of input.
            data_len = 0;
            free(data_buffer);
            data_buffer = malloc(BUFFER_SIZE);
            if (data_buffer == NULL)
            {
                perror("malloc new data_buffer");
                syslog(LOG_USER | LOG_ERR, "Error allocating buffer [%s]", strerror(errno));
                break;
            }

            // Write full file contents back to client.
            if (write_to_client(fd, conn_info->clfd) != 0)
            {
                syslog(LOG_USER | LOG_ERR, "Error writing to client");
                break;
            }
        }
    }
    close(fd);
    free(data_buffer);
    if (n < 0)
    {
        perror("Error reading from socket");
        syslog(LOG_USER | LOG_ERR, "Error reading from socket [%s]", strerror(errno));
        close(conn_info->clfd);
        conn_info->done = true;
        return NULL;
    }

    close(conn_info->clfd);
    syslog(LOG_USER | LOG_DEBUG, "Closed connection from %s", inet_ntoa(conn_info->cli_addr.sin_addr));
    conn_info->done = true;
    return NULL;
}

int write_to_client(int fd, int clfd)
{
    char read_buffer[BUFFER_SIZE];
    ssize_t n;
    while ((n = read(fd, read_buffer, BUFFER_SIZE)) > 0)
    {
        if (n < 0)
        {
            perror("read");
            syslog(LOG_USER | LOG_ERR, "Error reading file (%s) [%s]", FILENAME, strerror(errno));
            return -1;
        }
        // printf("Sending <%s> to client\n", read_buffer);
        ssize_t sent = send(clfd, read_buffer, n, 0);
        if (sent != n)
        {
            syslog(LOG_USER | LOG_ERR, "Read (%ld) and send (%ld) mismatch", n, sent);
            return -1;
        }
    }
    return 0;
}

int append_packet(const char *fname, const char *buf, size_t len)
{
    int fd = open(fname, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd == -1)
    {
        perror("Error opening file (filename: " FILENAME ")");
        syslog(LOG_USER | LOG_ERR, "Error opening file [%s]: %s", strerror(errno), FILENAME);
        return -1;
    }
    ssize_t res = write(fd, buf, len);
    if (res == -1)
    {
        perror("Error writing to file (filename: " FILENAME ")");
        syslog(LOG_USER | LOG_ERR, "Error writing to file [%s]: %s", strerror(errno), FILENAME);
        close(fd);
        return -1;
    }
    close(fd);
    return 0;
}