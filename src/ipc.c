/*
 * Pipes ordinarios UNIX (Silberschatz 3.6.3, figs. 3.25–3.26):
 * pipe() → fork() → cada proceso cierra el extremo que no usa.
 *
 * Daniel Mendez Romero — 2022437806
 */

#include "meeseeks_box.h"

#include <errno.h>
#include <unistd.h>

int pipe_pair(int fd[2])
{
    if (pipe(fd) == -1) {
        perror("pipe");
        return -1;
    }
    return 0;
}

int write_full(int fd, const void *buf, size_t n)
{
    const char *p = buf;
    size_t left = n;
    while (left > 0) {
        ssize_t w = write(fd, p, left);
        if (w < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (w == 0)
            return -1;
        p += (size_t)w;
        left -= (size_t)w;
    }
    return 0;
}

int read_full(int fd, void *buf, size_t n)
{
    char *p = buf;
    size_t left = n;
    while (left > 0) {
        ssize_t r = read(fd, p, left);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (r == 0)
            return 0; /* EOF */
        p += (size_t)r;
        left -= (size_t)r;
    }
    return 1;
}

void close_quiet(int fd)
{
    if (fd >= 0)
        close(fd);
}
