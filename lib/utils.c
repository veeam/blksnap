#include "utils.h"
#include <unistd.h>
#include <fcntl.h>

int generate_random(void* buf, unsigned int length)
{
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd == -1)
        return -1;

    int result = read(fd, buf, length);
    close(fd);
    return result;
}
