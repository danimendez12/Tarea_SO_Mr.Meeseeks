/*
 * Registro de mensajes, reloj y simulación de trabajo.
 * El tiempo de sleep NO forma parte del tiempo real reportado (consigna).
 *
 * Daniel Mendez Romero — 2022437806
 */

#include "meeseeks_box.h"

#include <errno.h>
#include <poll.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

double ns_to_sec(uint64_t ns)
{
    return (double)ns / 1000000000.0;
}

double rand_range(double lo, double hi)
{
    return lo + (hi - lo) * ((double)rand() / (double)RAND_MAX);
}

/*
 * Distribución no uniforme (Irwin-Hall de 2 uniformes):
 * tiende al centro, pero cubre 0..100. Caso 2 de la consigna.
 */
double random_difficulty(void)
{
    double u = ((double)rand() / RAND_MAX + (double)rand() / RAND_MAX) / 2.0;
    return u * 100.0;
}

void mx_log(shared_t *s, int n, int i, const char *fmt, ...)
{
    shared_lock(s);
    printf("[pid=%d ppid=%d N=%d i=%d] ", (int)getpid(), (int)getppid(), n, i);
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    if (fmt[0] && fmt[strlen(fmt) - 1] != '\n')
        putchar('\n');
    fflush(stdout);
    shared_unlock(s);
}

int abort_pending(int abort_r)
{
    if (abort_r < 0)
        return 0;
    struct pollfd p = { .fd = abort_r, .events = POLLIN };
    int r = poll(&p, 1, 0);
    return r > 0 && (p.revents & (POLLIN | POLLHUP | POLLERR));
}

int sleep_sim(box_t *box, int abort_r, double seconds, uint64_t *slept_ns)
{
    if (seconds < 0)
        seconds = 0;
    uint64_t start = now_ns();
    uint64_t target = (uint64_t)(seconds * 1000000000.0);
    while (1) {
        if (box->shared && box->shared->stop_all)
            break;
        if (abort_pending(abort_r))
            break;
        uint64_t elapsed = now_ns() - start;
        if (elapsed >= target)
            break;
        uint64_t left = target - elapsed;
        if (left > 100000000ull)
            left = 100000000ull;
        struct timespec ts = {
            .tv_sec = (time_t)(left / 1000000000ull),
            .tv_nsec = (long)(left % 1000000000ull)
        };
        nanosleep(&ts, NULL);
    }
    uint64_t slept = now_ns() - start;
    if (slept_ns)
        *slept_ns = slept;
    if (box->shared) {
        shared_lock(box->shared);
        if (slept > box->shared->sim_ns_max)
            box->shared->sim_ns_max = slept;
        shared_unlock(box->shared);
    }
    if (abort_pending(abort_r) || (box->shared && box->shared->stop_all))
        return 1;
    return 0;
}
