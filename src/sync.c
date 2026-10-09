/*
 * Memoria compartida + mutex process-shared (Silberschatz 5.5 / 5.6)
 * y semáforo POSIX con nombre como semáforo CONTADOR (cupo de procesos).
 *
 * Daniel Mendez Romero — 2022437806
 */

#include "meeseeks_box.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif

shared_t *shared_create(void)
{
    shared_t *s = mmap(NULL, sizeof(*s), PROT_READ | PROT_WRITE,
                       MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (s == MAP_FAILED) {
        perror("mmap");
        return NULL;
    }
    memset(s, 0, sizeof(*s));
    s->next_instance = 1;

    pthread_mutexattr_t attr;
    if (pthread_mutexattr_init(&attr) != 0) {
        perror("pthread_mutexattr_init");
        munmap(s, sizeof(*s));
        return NULL;
    }
    /* El mutex debe sincronizar procesos hermanos tras fork(). */
    if (pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED) != 0) {
        perror("pthread_mutexattr_setpshared");
        pthread_mutexattr_destroy(&attr);
        munmap(s, sizeof(*s));
        return NULL;
    }
    if (pthread_mutex_init(&s->lock, &attr) != 0) {
        perror("pthread_mutex_init");
        pthread_mutexattr_destroy(&attr);
        munmap(s, sizeof(*s));
        return NULL;
    }
    pthread_mutexattr_destroy(&attr);
    s->ready = 1;
    return s;
}

void shared_destroy(shared_t *s)
{
    if (!s)
        return;
    if (s->ready)
        pthread_mutex_destroy(&s->lock);
    munmap(s, sizeof(*s));
}

void shared_lock(shared_t *s)
{
    if (s && s->ready)
        pthread_mutex_lock(&s->lock);
}

void shared_unlock(shared_t *s)
{
    if (s && s->ready)
        pthread_mutex_unlock(&s->lock);
}

int slots_open(box_t *box, unsigned value)
{
    snprintf(box->slots_name, sizeof(box->slots_name),
             "/meeseeks_slots_%d", (int)getpid());
    sem_unlink(box->slots_name);
    box->slots = sem_open(box->slots_name, O_CREAT | O_EXCL, 0600, value);
    if (box->slots == SEM_FAILED) {
        perror("sem_open");
        box->slots = NULL;
        return -1;
    }
    return 0;
}

void slots_close(box_t *box)
{
    if (box->slots && box->slots != SEM_FAILED) {
        sem_close(box->slots);
        box->slots = NULL;
    }
    if (box->slots_name[0]) {
        sem_unlink(box->slots_name);
        box->slots_name[0] = '\0';
    }
}

int slot_acquire(box_t *box)
{
    /*
     * wait() de Silberschatz sobre un semáforo contador.
     * trywait: si no hay cupo (dificultad 0) no nos bloqueamos para siempre.
     */
    if (!box->slots)
        return 0;
    if (sem_trywait(box->slots) == -1) {
        if (errno == EAGAIN)
            return -1;
        perror("sem_trywait");
        return -1;
    }
    return 0;
}

void slot_release(box_t *box)
{
    if (box->slots)
        sem_post(box->slots);
}
