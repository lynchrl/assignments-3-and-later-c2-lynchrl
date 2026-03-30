// Small test program to verify aesdchar ioctl functionality.
// Simply takes command line args corresponding to aesd_seekto.write_cmd and
// aesd_seekto.write_cmd_offset, performs the ioctl, and prints the return result.

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

#include "aesd_ioctl.h"

#define DEVICE_PATH "/dev/aesdchar"

int main(int argc, char *argv[])
{
    // Get command line args for write_cmd and write_cmd_offset
    if (argc != 3)
    {
        fprintf(stderr, "Usage: %s <write_cmd> <write_cmd_offset>\n", argv[0]);
        return 1;
    }

    // Open the device using open so we have the fd for ioctl and a read.
    int fd = open(DEVICE_PATH, O_RDWR);
    if (fd < 0)
    {
        perror("Failed to open device");
        return 1;
    }

    // Create an aesd_seekto struct and populate it with the command line args
    struct aesd_seekto seekto;
    seekto.write_cmd = atoi(argv[1]);
    seekto.write_cmd_offset = atoi(argv[2]);

    // Perform the ioctl call and print the result
    int result = ioctl(fd, AESDCHAR_IOCSEEKTO, &seekto);
    if (result != 0)
    {
        perror("ioctl failed");
        fprintf(stderr, "ioctl failed with error: %d\n", result);
        close(fd);
        return 1;
    }

    // Read the single character at the new file position to verify the seek worked
    char buf;
    ssize_t c = read(fd, &buf, 1);
    if (c < 0)
    {
        perror("Failed to read from device");
        close(fd);
        return 1;
    }

    printf("ioctl returned: %d\n", result);
    printf("Read character at new file position: %c\n", buf);
    close(fd);

    return 0;
}