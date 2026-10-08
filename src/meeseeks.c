/*
 * Un Mr. Meeseeks es un PROCESO (fork). El mismo código corre en todos.
 * Comunicación: pipes. Contadores: memoria compartida + mutex.
 *
 * Daniel Mendez Romero — 2022437806
 */

#include "meeseeks_box.h"

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct {
    pid_t pid;
    int   res_r;
    int   abort_w;
} helper_t;

static void register_meeseeks(shared_t *s, int n, int i)
{
    (void)i;
    shared_lock(s);
    s->meeseeks_created++;
    s->task_created++;
    s->live++;
    if (n > s->max_level_seen)
        s->max_level_seen = n;
    s->next_instance++;
    shared_unlock(s);
}

static void unregister_meeseeks(shared_t *s)
{
    shared_lock(s);
    if (s->live > 0)
        s->live--;
    shared_unlock(s);
}

static void fill_self(result_msg_t *r, res_status_t st, int n, int i, const char *d)
{
    memset(r, 0, sizeof(*r));
    r->status = st;
    r->solver_pid = getpid();
    r->solver_ppid = getppid();
    r->solver_n = n;
    r->solver_i = i;
    if (d)
        snprintf(r->detail, sizeof(r->detail), "%s", d);
}

static void say_hello(shared_t *s, int n, int i)
{
    shared_lock(s);
    printf("Hi, I'm Mr. Meeseeks! Look at Meeeee. (%d, %d, %d, %d)\n",
           (int)getpid(), (int)getppid(), n, i);
    fflush(stdout);
    shared_unlock(s);
}

static void say_bye(box_t *box, int n, int i, const char *why)
{
    mx_log(box->shared, n, i, "Caaann doooo... goodbye! (%s)", why);
}

static int send_result(int res_w, const result_msg_t *r)
{
    return write_full(res_w, r, sizeof(*r));
}

static int run_exec(box_t *box, const request_msg_t *req, result_msg_t *out)
{
    char buf[MSG_TEXT];
    snprintf(buf, sizeof(buf), "%s", req->text);

    char *argv[32];
    int argc = 0;
    char *save = NULL;
    for (char *tok = strtok_r(buf, " \t", &save); tok && argc < 31;
         tok = strtok_r(NULL, " \t", &save))
        argv[argc++] = tok;
    argv[argc] = NULL;
    if (argc == 0) {
        fill_self(out, RES_FAILED, req->n, req->i, "comando vacío");
        return 0;
    }

    mx_log(box->shared, req->n, req->i, "ejecutando programa externo: %s", req->text);

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        fill_self(out, RES_FAILED, req->n, req->i, "fork falló para exec");
        return 0;
    }
    if (pid == 0) {
        execvp(argv[0], argv);
        perror("execvp");
        _exit(127);
    }
    int st = 0;
    if (waitpid(pid, &st, 0) < 0) {
        perror("waitpid");
        fill_self(out, RES_FAILED, req->n, req->i, "waitpid falló");
        return 0;
    }
    /* Convención UNIX: 0 = éxito. Cualquier otro código = error. */
    if (WIFEXITED(st) && WEXITSTATUS(st) == 0) {
        fill_self(out, RES_SOLVED, req->n, req->i, "programa terminó con éxito (0)");
        mx_log(box->shared, req->n, req->i,
               "programa externo OK. Gracias por dejarme ayudar. Existence is pain!");
    } else {
        char msg[MSG_TEXT];
        if (WIFEXITED(st))
            snprintf(msg, sizeof(msg), "programa falló, código=%d", WEXITSTATUS(st));
        else if (WIFSIGNALED(st))
            snprintf(msg, sizeof(msg), "programa matado por señal %d", WTERMSIG(st));
        else
            snprintf(msg, sizeof(msg), "programa no terminó con éxito");
        fill_self(out, RES_FAILED, req->n, req->i, msg);
        mx_log(box->shared, req->n, req->i, "%s", msg);
    }
    return 0;
}

static int run_calc(box_t *box, const request_msg_t *req, result_msg_t *out)
{
    long val = 0;
    char err[128];
    mx_log(box->shared, req->n, req->i, "calculando: %s", req->text);
    if (calc_eval(req->text, &val, err, sizeof(err)) != 0) {
        fill_self(out, RES_FAILED, req->n, req->i, err);
        mx_log(box->shared, req->n, req->i, "error de cálculo: %s", err);
        return 0;
    }
    char msg[MSG_TEXT];
    snprintf(msg, sizeof(msg), "%s = %ld", req->text, val);
    fill_self(out, RES_SOLVED, req->n, req->i, msg);
    mx_log(box->shared, req->n, req->i,
           "solicitud de cálculo completada: %s. ¡Gracias! Can do!", msg);
    return 0;
}

static double dilute(double d)
{
    if (d <= 0.0)
        return 0.0; /* imposible se mantiene imposible */
    double nd = d + AGING_FACTOR * (100.0 - d);
    if (nd > 100.0)
        nd = 100.0;
    return nd;
}

static int how_many_helpers(double d)
{
    int n = 0;
    if (d > 85.0)
        return 0;
    if (d <= 45.0)
        n = 5;
    else
        n = 2;
    /* Más difíciles → más ayudantes extra (probabilidad / video de Meeseeks). */
    int extra = (int)((85.0 - d) / 20.0);
    if (extra < 0)
        extra = 0;
    n += extra;
    if (n > MAX_HELPERS)
        n = MAX_HELPERS;
    return n;
}

static void abort_helpers(helper_t *h, int n)
{
    char c = 'A';
    for (int i = 0; i < n; i++) {
        if (h[i].abort_w >= 0) {
            write_full(h[i].abort_w, &c, 1);
            close_quiet(h[i].abort_w);
            h[i].abort_w = -1;
        }
    }
}

static void reap_helpers(helper_t *h, int n)
{
    for (int i = 0; i < n; i++) {
        close_quiet(h[i].res_r);
        h[i].res_r = -1;
        close_quiet(h[i].abort_w);
        h[i].abort_w = -1;
        if (h[i].pid > 0) {
            int st;
            waitpid(h[i].pid, &st, 0);
            h[i].pid = -1;
        }
    }
}

static int wait_helpers(box_t *box, helper_t *h, int nh, int abort_r,
                        result_msg_t *out, int n, int i)
{
    int remaining = nh;
    int got = 0;
    result_msg_t last_fail;
    memset(&last_fail, 0, sizeof(last_fail));
    last_fail.status = RES_FAILED;

    while (remaining > 0) {
        if (box->shared->stop_all || abort_pending(abort_r)) {
            mx_log(box->shared, n, i, "abortando ayudantes (caos o un hermano resolvió)");
            abort_helpers(h, nh);
            reap_helpers(h, nh);
            fill_self(out, RES_ABORTED, n, i, "abortado");
            return 0;
        }

        struct pollfd pf[MAX_HELPERS + 1];
        int map[MAX_HELPERS];
        int n_help_fds = 0;
        for (int k = 0; k < nh; k++) {
            if (h[k].res_r < 0)
                continue;
            pf[n_help_fds].fd = h[k].res_r;
            pf[n_help_fds].events = POLLIN;
            map[n_help_fds] = k;
            n_help_fds++;
        }
        int nf = n_help_fds;
        if (abort_r >= 0) {
            pf[nf].fd = abort_r;
            pf[nf].events = POLLIN;
            nf++;
        }
        int pr = poll(pf, (nfds_t)nf, 200);
        if (pr < 0) {
            if (errno == EINTR)
                continue;
            perror("poll");
            break;
        }
        if (abort_pending(abort_r) || box->shared->stop_all) {
            abort_helpers(h, nh);
            reap_helpers(h, nh);
            fill_self(out, RES_ABORTED, n, i, "abortado");
            return 0;
        }
        for (int k = 0; k < n_help_fds; k++) {
            int idx = map[k];
            if (!(pf[k].revents & (POLLIN | POLLHUP)))
                continue;
            if (h[idx].res_r < 0)
                continue;
            result_msg_t child;
            int rr = read_full(h[idx].res_r, &child, sizeof(child));
            close_quiet(h[idx].res_r);
            h[idx].res_r = -1;
            remaining--;
            if (rr != 1)
                continue;
            got++;
            if (child.status == RES_SOLVED) {
                mx_log(box->shared, n, i,
                       "un hijo resolvió (pid=%d N=%d i=%d); aviso a hermanos",
                       (int)child.solver_pid, child.solver_n, child.solver_i);
                abort_helpers(h, nh);
                reap_helpers(h, nh);
                *out = child; /* se propaga quién resolvió */
                return 0;
            }
            last_fail = child;
        }
    }
    reap_helpers(h, nh);
    if (got == 0) {
        fill_self(out, RES_FAILED, n, i, "sin respuesta de ayudantes");
        return 0;
    }
    *out = last_fail;
    if (out->status == RES_SOLVED)
        return 0;
    if (last_fail.status == RES_IMPOSSIBLE)
        out->status = RES_IMPOSSIBLE;
    else
        out->status = RES_FAILED;
    fill_self(out, out->status, n, i, last_fail.detail[0] ? last_fail.detail : "nadie pudo");
    return 0;
}

static int spawn_helpers(box_t *box, const request_msg_t *parent_req,
                         int abort_r, int count, double child_d, result_msg_t *out)
{
    helper_t h[MAX_HELPERS];
    int spawned = 0;

    mx_log(box->shared, parent_req->n, parent_req->i,
           "necesito ayuda: crearé %d Mr. Meeseeks (N=%d, d_hijo=%.2f)",
           count, parent_req->n + 1, child_d);

    for (int hi = 0; hi < count; hi++) {
        if (box->shared->stop_all || abort_pending(abort_r))
            break;
        if (parent_req->n + 1 > parent_req->max_depth) {
            mx_log(box->shared, parent_req->n, parent_req->i,
                   "contingencia: no creo más hijos (max profundidad=%d)",
                   parent_req->max_depth);
            break;
        }
        if (slot_acquire(box) < 0) {
            mx_log(box->shared, parent_req->n, parent_req->i,
                   "contingencia: semáforo de cupo en 0 (max_live). No más fork.");
            break;
        }

        int reqp[2], resp[2], abp[2];
        if (pipe_pair(reqp) < 0 || pipe_pair(resp) < 0 || pipe_pair(abp) < 0) {
            slot_release(box);
            break;
        }

        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            close_quiet(reqp[0]); close_quiet(reqp[1]);
            close_quiet(resp[0]); close_quiet(resp[1]);
            close_quiet(abp[0]); close_quiet(abp[1]);
            slot_release(box);
            break;
        }
        if (pid == 0) {
            close_quiet(reqp[PIPE_WRITE]);
            close_quiet(resp[PIPE_READ]);
            close_quiet(abp[PIPE_WRITE]);
            /* Cerrar copias de hermanos ya creados. */
            for (int k = 0; k < spawned; k++) {
                close_quiet(h[k].res_r);
                close_quiet(h[k].abort_w);
            }
            int rc = meeseeks_main(box, reqp[PIPE_READ], resp[PIPE_WRITE],
                                   abp[PIPE_READ]);
            _exit(rc == 0 ? 0 : 1);
        }

        close_quiet(reqp[PIPE_READ]);
        close_quiet(resp[PIPE_WRITE]);
        close_quiet(abp[PIPE_READ]);

        request_msg_t child = *parent_req;
        child.n = parent_req->n + 1;
        child.i = hi + 1;
        child.difficulty = child_d;
        if (write_full(reqp[PIPE_WRITE], &child, sizeof(child)) < 0)
            mx_log(box->shared, parent_req->n, parent_req->i,
                   "error escribiendo pipe de solicitud al hijo i=%d", hi + 1);
        close_quiet(reqp[PIPE_WRITE]);

        h[spawned].pid = pid;
        h[spawned].res_r = resp[PIPE_READ];
        h[spawned].abort_w = abp[PIPE_WRITE];
        spawned++;
    }

    if (spawned == 0) {
        fill_self(out, parent_req->difficulty <= 0 ? RES_IMPOSSIBLE : RES_FAILED,
                  parent_req->n, parent_req->i, "no se pudieron crear ayudantes");
        return 0;
    }
    return wait_helpers(box, h, spawned, abort_r, out, parent_req->n, parent_req->i);
}

static int run_textual(box_t *box, const request_msg_t *req, int abort_r,
                       result_msg_t *out)
{
    double sim = req->sim_seconds;
    if (sim <= 0)
        sim = rand_range(0.5, 5.0);

    mx_log(box->shared, req->n, req->i,
           "solicitud textual \"%s\" d=%.2f; simularé %.2fs (ese tiempo NO se reporta)",
           req->text, req->difficulty, sim);

    uint64_t slept = 0;
    if (sleep_sim(box, abort_r, sim, &slept)) {
        fill_self(out, RES_ABORTED, req->n, req->i, "abortado durante simulación");
        out->sim_ns = slept;
        say_bye(box, req->n, req->i, "me abortaron");
        return 0;
    }

    double d = req->difficulty;
    /*
     * Creativo: "determinación de Meeseeks" = d/100.
     * Los rangos de la consigna mandan cuántos ayudantes mínimos.
     * Aging: los hijos reciben d diluida hacia 100 (más fácil).
     * d==0 nunca se diluye ni se resuelve.
     */
    if (d > 85.0) {
        fill_self(out, RES_SOLVED, req->n, req->i, "I'm Mr. Meeseeks! Look at me!");
        out->sim_ns = slept;
        mx_log(box->shared, req->n, req->i,
               "solicitud completada. Gracias por dejarme servir. Existence is pain!");
        return 0;
    }

    int helpers = how_many_helpers(d);
    if (req->n >= req->max_depth) {
        mx_log(box->shared, req->n, req->i,
               "contingencia: profundidad máxima. d=%.2f %s",
               d, d <= 0 ? "(imposible: no hay solución)" : "");
        fill_self(out, d <= 0 ? RES_IMPOSSIBLE : RES_FAILED,
                  req->n, req->i, "tope de profundidad");
        out->sim_ns = slept;
        say_bye(box, req->n, req->i, "no pude");
        return 0;
    }

    double child_d = dilute(d);
    mx_log(box->shared, req->n, req->i,
           "evalué ayuda (d=%.2f → %d hijos, aging d'=%.2f)", d, helpers, child_d);
    spawn_helpers(box, req, abort_r, helpers, child_d, out);
    out->sim_ns += slept;

    if (out->status == RES_SOLVED) {
        mx_log(box->shared, req->n, req->i,
               "propago éxito de pid=%d (N=%d,i=%d) hacia mi padre y me despido",
               (int)out->solver_pid, out->solver_n, out->solver_i);
        say_bye(box, req->n, req->i, "misión cumplida por un descendiente");
    } else {
        say_bye(box, req->n, req->i, "no se resolvió aquí");
    }
    return 0;
}

int meeseeks_main(box_t *box, int req_r, int res_w, int abort_r)
{
    srand((unsigned)(now_ns() ^ (uint64_t)getpid()));

    request_msg_t req;
    memset(&req, 0, sizeof(req));
    if (read_full(req_r, &req, sizeof(req)) != 1) {
        result_msg_t r;
        fill_self(&r, RES_FAILED, 0, 0, "no llegó la solicitud por pipe");
        send_result(res_w, &r);
        close_quiet(req_r);
        close_quiet(res_w);
        close_quiet(abort_r);
        slot_release(box);
        return 1;
    }
    close_quiet(req_r);

    register_meeseeks(box->shared, req.n, req.i);
    say_hello(box->shared, req.n, req.i);

    result_msg_t out;
    fill_self(&out, RES_FAILED, req.n, req.i, "");

    if (box->shared->stop_all || abort_pending(abort_r)) {
        fill_self(&out, RES_ABORTED, req.n, req.i, "abortado al nacer");
        say_bye(box, req.n, req.i, "caos planetario");
    } else if (req.kind == REQ_CALC) {
        run_calc(box, &req, &out);
    } else if (req.kind == REQ_EXEC) {
        run_exec(box, &req, &out);
    } else {
        run_textual(box, &req, abort_r, &out);
    }

    send_result(res_w, &out);
    close_quiet(res_w);
    close_quiet(abort_r);
    unregister_meeseeks(box->shared);
    slot_release(box);
    return 0;
}

int spawn_first_meeseeks(box_t *box, const request_msg_t *req,
                         result_msg_t *out, double timeout_sec)
{
    if (slot_acquire(box) < 0) {
        fprintf(stderr, "sin cupo para el primer Mr. Meeseeks\n");
        return -1;
    }

    int reqp[2], resp[2], abp[2];
    if (pipe_pair(reqp) < 0 || pipe_pair(resp) < 0 || pipe_pair(abp) < 0) {
        slot_release(box);
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        slot_release(box);
        return -1;
    }
    if (pid == 0) {
        /* Grupo de procesos propio para poder matar el árbol en timeout. */
        setpgid(0, 0);
        close_quiet(reqp[PIPE_WRITE]);
        close_quiet(resp[PIPE_READ]);
        close_quiet(abp[PIPE_WRITE]);
        int rc = meeseeks_main(box, reqp[PIPE_READ], resp[PIPE_WRITE],
                               abp[PIPE_READ]);
        _exit(rc == 0 ? 0 : 1);
    }
    setpgid(pid, pid);

    close_quiet(reqp[PIPE_READ]);
    close_quiet(resp[PIPE_WRITE]);
    close_quiet(abp[PIPE_READ]);

    if (write_full(reqp[PIPE_WRITE], req, sizeof(*req)) < 0)
        perror("write solicitud");
    close_quiet(reqp[PIPE_WRITE]);

    int timeout_ms = (timeout_sec <= 0) ? -1 : (int)(timeout_sec * 1000.0);
    struct pollfd p = { .fd = resp[PIPE_READ], .events = POLLIN };
    int pr = poll(&p, 1, timeout_ms);

    if (pr == 0 && !box->experiment) {
        fprintf(stderr,
                "\n*** CAOS PLANETARIO *** timeout de %.1fs. Terminando el árbol (pgid=%d).\n",
                timeout_sec, (int)pid);
        box->shared->stop_all = 1;
        kill(-pid, SIGTERM);
        sleep(1);
        kill(-pid, SIGKILL);
        int st;
        waitpid(pid, &st, 0);
        close_quiet(resp[PIPE_READ]);
        close_quiet(abp[PIPE_WRITE]);
        memset(out, 0, sizeof(*out));
        out->status = RES_FAILED;
        snprintf(out->detail, sizeof(out->detail), "caos planetario (timeout)");
        return 1;
    }
    if (pr == 0 && box->experiment) {
        fprintf(stderr,
                "\n[experimento] venció el tiempo máximo (%.1fs) y NO se mata el árbol.\n"
                "Midiendo tiempo adicional hasta terminar...\n",
                timeout_sec);
        poll(&p, 1, -1);
    }

    int rr = read_full(resp[PIPE_READ], out, sizeof(*out));
    close_quiet(resp[PIPE_READ]);
    close_quiet(abp[PIPE_WRITE]);
    int st;
    waitpid(pid, &st, 0);
    if (rr != 1) {
        memset(out, 0, sizeof(*out));
        out->status = RES_FAILED;
        snprintf(out->detail, sizeof(out->detail), "sin resultado del Meeseeks original");
        return -1;
    }
    return 0;
}
