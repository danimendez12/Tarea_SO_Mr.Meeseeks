/*
 * Tarea 3 — Sistemas Operativos
 * Box de Mr. Meeseeks
 *
 * Estudiante : Daniel Mendez Romero
 * Carné      : 2022437806
 * Curso      : Sistemas Operativos
 * Profesor   : M.Sc. Esteban Arias-Méndez
 *
 * Referencias (Silberschatz, OSC 9th):
 *  Cap. 3  fork(), wait()/waitpid(), exec, pipes ordinarios
 *  Cap. 5  mutex (semáforo binario) y semáforo contador
 *  Cap. 6  aging (dilución de dificultad)
 * Consigna: pthreads para el mutex process-shared; pipes para mensajes.
 */

#ifndef MEESEEKS_BOX_H
#define MEESEEKS_BOX_H

#define _POSIX_C_SOURCE 200809L

#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <sys/types.h>
#include <stddef.h>
#include <stdint.h>

#define MSG_TEXT              256
#define MAX_HELPERS           16
#define DEFAULT_TIMEOUT_SEC   300
#define DEFAULT_MAX_LIVE      64
#define DEFAULT_MAX_DEPTH     8
#define PIPE_READ             0
#define PIPE_WRITE            1
#define AGING_FACTOR          0.40

typedef enum {
    REQ_TEXTUAL = 1,
    REQ_CALC    = 2,
    REQ_EXEC    = 3
} req_kind_t;

typedef enum {
    RES_SOLVED     = 1,
    RES_FAILED     = 2,
    RES_ABORTED    = 3,
    RES_IMPOSSIBLE = 4
} res_status_t;

/* La solicitud viaja SOLO por pipe, nunca por una variable global. */
typedef struct {
    req_kind_t kind;
    char       text[MSG_TEXT];
    double     difficulty;
    int        n;
    int        i;
    int        max_depth;
    int        max_live;
    double     sim_seconds;
    int        task_id;
} request_msg_t;

typedef struct {
    res_status_t status;
    pid_t        solver_pid;
    pid_t        solver_ppid;
    int          solver_n;
    int          solver_i;
    char         detail[MSG_TEXT];
    uint64_t     sim_ns;
} result_msg_t;

/*
 * Memoria compartida (mmap MAP_SHARED) para contadores N/i.
 * Mutex pthreads con PTHREAD_PROCESS_SHARED = semáforo binario entre procesos.
 */
typedef struct {
    pthread_mutex_t lock;
    int             ready;
    int             max_level_seen;
    int             next_instance;
    int             meeseeks_created;
    int             task_created;
    int             live;
    int             stop_all;
    uint64_t        sim_ns_max;
} shared_t;

typedef struct {
    double    timeout_sec;
    int       diff_mode;
    int       max_live;
    int       max_depth;
    int       experiment;
    double    default_sim;
    shared_t *shared;
    sem_t    *slots;
    char      slots_name[64];
} box_t;

typedef struct {
    int    n_tasks;
    int    n_ok;
    int    n_fail;
    double duration_sec[128];
    int    created[128];
    int    kind[128];
    int    finished_ok[128];
    char   summary[128][MSG_TEXT];
} report_t;

shared_t *shared_create(void);
void      shared_destroy(shared_t *s);
int       slots_open(box_t *box, unsigned value);
void      slots_close(box_t *box);
int       slot_acquire(box_t *box);
void      slot_release(box_t *box);
void      shared_lock(shared_t *s);
void      shared_unlock(shared_t *s);

int  pipe_pair(int fd[2]);
int  write_full(int fd, const void *buf, size_t n);
int  read_full(int fd, void *buf, size_t n);
void close_quiet(int fd);

int meeseeks_main(box_t *box, int req_r, int res_w, int abort_r);

int calc_eval(const char *expr, long *out, char *err, size_t errlen);

void     mx_log(shared_t *s, int n, int i, const char *fmt, ...);
uint64_t now_ns(void);
double   ns_to_sec(uint64_t ns);
int      sleep_sim(box_t *box, int abort_r, double seconds, uint64_t *slept_ns);
double   rand_range(double lo, double hi);
double   random_difficulty(void);
int      abort_pending(int abort_r);
int      spawn_first_meeseeks(box_t *box, const request_msg_t *req,
                              result_msg_t *out, double timeout_sec);

#endif
