/*
 * Box de Mr. Meeseeks — proceso padre.
 * Recibe solicitudes, crea el primer Meeseeks con fork(), espera, informa.
 *
 * Daniel Mendez Romero — 2022437806
 * Sistemas Operativos — Prof. M.Sc. Esteban Arias-Méndez
 */

#include "meeseeks_box.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

static void print_banner(void)
{
    printf("==============================================\n");
    printf("   Box de Mr. Meeseeks  (Tarea 2 — SO)\n");
    printf("   Daniel Mendez Romero  2022437806\n");
    printf("==============================================\n");
}

static void print_menu(const box_t *box)
{
    printf("\n--- Menú ---\n");
    printf("1) Solicitud textual\n");
    printf("2) Cálculo aritmético/lógico\n");
    printf("3) Ejecutar programa externo\n");
    printf("4) Configuración  (timeout=%.0fs, dificultad=%s, max_live=%d, max_N=%d, exp=%s)\n",
           box->timeout_sec,
           box->diff_mode == 1 ? "usuario" : "aleatoria",
           box->max_live, box->max_depth,
           box->experiment ? "sí" : "no");
    printf("5) Adiós (cerrar Box e informe)\n");
    printf("Opción: ");
    fflush(stdout);
}

static void trim_nl(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) {
        s[--n] = '\0';
    }
}

static int read_line(const char *prompt, char *buf, size_t n)
{
    if (prompt) {
        printf("%s", prompt);
        fflush(stdout);
    }
    if (!fgets(buf, (int)n, stdin))
        return -1;
    trim_nl(buf);
    return 0;
}

static double ask_difficulty(box_t *box)
{
    if (box->diff_mode == 2) {
        double d = random_difficulty();
        printf("Mr. Meeseeks eligió dificultad aleatoria: %.2f\n", d);
        return d;
    }
    char buf[64];
    for (;;) {
        if (read_line("Dificultad (0=imposible ... 100=muy fácil): ", buf, sizeof(buf)) < 0)
            return 100.0;
        char *end = NULL;
        double d = strtod(buf, &end);
        if (end != buf && d >= 0.0 && d <= 100.0)
            return d;
        printf("Valor inválido. Use 0..100.\n");
    }
}

static double ask_sim(box_t *box)
{
    char buf[64];
    if (read_line("Tiempo de simulación en segundos (Enter = aleatorio 0.5-5, o el default): ",
                  buf, sizeof(buf)) < 0)
        return 0;
    if (buf[0] == '\0')
        return box->default_sim;
    double s = strtod(buf, NULL);
    if (s < 0)
        s = 0;
    return s;
}

static void record(report_t *rep, req_kind_t kind, int ok, double dur, int created,
                   const char *sum)
{
    if (rep->n_tasks >= 128)
        return;
    int i = rep->n_tasks++;
    rep->kind[i] = (int)kind;
    rep->finished_ok[i] = ok;
    rep->duration_sec[i] = dur;
    rep->created[i] = created;
    snprintf(rep->summary[i], sizeof(rep->summary[i]), "%s", sum ? sum : "");
    if (ok)
        rep->n_ok++;
    else
        rep->n_fail++;
}

static void print_report(const report_t *rep, const box_t *box)
{
    printf("\n============== INFORME DEL BOX ==============\n");
    printf("Tareas solicitadas : %d\n", rep->n_tasks);
    printf("Completadas        : %d\n", rep->n_ok);
    printf("No completadas     : %d\n", rep->n_fail);
    printf("Meeseeks totales   : %d\n", box->shared->meeseeks_created);
    for (int i = 0; i < rep->n_tasks; i++) {
        const char *k = "textual";
        if (rep->kind[i] == REQ_CALC)
            k = "cálculo";
        else if (rep->kind[i] == REQ_EXEC)
            k = "externo";
        printf("  [%d] %s  t_real=%.4fs  meeseeks=%d  %s  %s\n",
               i + 1, k, rep->duration_sec[i], rep->created[i],
               rep->finished_ok[i] ? "OK" : "NO",
               rep->summary[i]);
    }
    printf("=============================================\n");
    printf("Look at me! The Box is closed.\n");
}

static int run_request(box_t *box, report_t *rep, request_msg_t *req)
{
    shared_lock(box->shared);
    box->shared->task_created = 0;
    box->shared->sim_ns_max = 0;
    box->shared->stop_all = 0;
    shared_unlock(box->shared);

    uint64_t t0 = now_ns();
    result_msg_t out;
    memset(&out, 0, sizeof(out));
    int chaos = spawn_first_meeseeks(box, req, &out, box->timeout_sec);
    uint64_t wall = now_ns() - t0;

    uint64_t sim_max = 0;
    int created = 0;
    shared_lock(box->shared);
    sim_max = box->shared->sim_ns_max;
    created = box->shared->task_created;
    shared_unlock(box->shared);

    /* Tiempo real = pared - mayor espera de simulación (consigna). */
    uint64_t real_ns = wall > sim_max ? wall - sim_max : 0;
    double real_s = ns_to_sec(real_ns);

    int ok = (out.status == RES_SOLVED);
    if (ok) {
        printf("\n[Box] Solicitud RESUELTA por Mr. Meeseeks "
               "(pid=%d, ppid=%d, N=%d, i=%d)\n",
               (int)out.solver_pid, (int)out.solver_ppid,
               out.solver_n, out.solver_i);
        if (out.detail[0])
            printf("[Box] Detalle: %s\n", out.detail);
    } else if (chaos == 1) {
        printf("\n[Box] Caos planetario. Tarea NO completada.\n");
    } else {
        printf("\n[Box] Solicitud NO completada (%s)\n",
               out.detail[0] ? out.detail : "fallo");
    }
    printf("[Box] t_pared=%.4fs  t_sim_max=%.4fs  t_real=%.4fs  meeseeks=%d\n",
           ns_to_sec(wall), ns_to_sec(sim_max), real_s, created);

    record(rep, req->kind, ok, real_s, created, out.detail);
    return ok;
}

static void cfg_menu(box_t *box)
{
    char buf[64];
    printf("1=dificultad por usuario, 2=aleatoria [%d]: ", box->diff_mode);
    if (fgets(buf, sizeof(buf), stdin)) {
        trim_nl(buf);
        if (buf[0] == '1' || buf[0] == '2')
            box->diff_mode = buf[0] - '0';
    }
    printf("Timeout en segundos (>=300 recomendado) [%.0f]: ", box->timeout_sec);
    if (fgets(buf, sizeof(buf), stdin)) {
        trim_nl(buf);
        if (buf[0]) {
            double t = strtod(buf, NULL);
            if (t > 0)
                box->timeout_sec = t;
        }
    }
    printf("Máximo de Meeseeks vivos (contingencia) [%d]: ", box->max_live);
    if (fgets(buf, sizeof(buf), stdin)) {
        trim_nl(buf);
        if (buf[0]) {
            int v = atoi(buf);
            if (v > 0)
                box->max_live = v;
        }
    }
    printf("Profundidad máxima N [%d]: ", box->max_depth);
    if (fgets(buf, sizeof(buf), stdin)) {
        trim_nl(buf);
        if (buf[0]) {
            int v = atoi(buf);
            if (v > 0)
                box->max_depth = v;
        }
    }
    printf("Modo experimento 0x02 (no matar al timeout) 0/1 [%d]: ", box->experiment);
    if (fgets(buf, sizeof(buf), stdin)) {
        trim_nl(buf);
        if (buf[0] == '0' || buf[0] == '1')
            box->experiment = buf[0] - '0';
    }
}

static int interactive(box_t *box, report_t *rep)
{
    char buf[MSG_TEXT];
    print_banner();
    for (;;) {
        print_menu(box);
        if (read_line(NULL, buf, sizeof(buf)) < 0)
            break;
        if (strcmp(buf, "5") == 0 || strcasecmp(buf, "adios") == 0 ||
            strcasecmp(buf, "adiós") == 0 || strcasecmp(buf, "salir") == 0)
            break;
        if (strcmp(buf, "4") == 0) {
            cfg_menu(box);
            continue;
        }

        request_msg_t req;
        memset(&req, 0, sizeof(req));
        req.n = 1;
        req.i = 1;
        req.max_depth = box->max_depth;
        req.max_live = box->max_live;
        req.task_id = rep->n_tasks + 1;

        if (strcmp(buf, "1") == 0) {
            req.kind = REQ_TEXTUAL;
            if (read_line("Solicitud: ", req.text, sizeof(req.text)) < 0)
                break;
            req.difficulty = ask_difficulty(box);
            req.sim_seconds = ask_sim(box);
            run_request(box, rep, &req);
        } else if (strcmp(buf, "2") == 0) {
            req.kind = REQ_CALC;
            req.difficulty = 100;
            if (read_line("Expresión: ", req.text, sizeof(req.text)) < 0)
                break;
            run_request(box, rep, &req);
        } else if (strcmp(buf, "3") == 0) {
            req.kind = REQ_EXEC;
            req.difficulty = 100;
            if (read_line("Comando (ej. /bin/echo hola  o  false): ",
                          req.text, sizeof(req.text)) < 0)
                break;
            run_request(box, rep, &req);
        } else {
            printf("Opción no válida.\n");
        }
    }
    print_report(rep, box);
    return 0;
}

static void usage(const char *argv0)
{
    fprintf(stderr,
            "Uso: %s [opciones]\n"
            "  (sin args)              menú interactivo\n"
            "  --text \"solicitud\"      una solicitud textual y sale\n"
            "  --calc \"2+2\"            un cálculo y sale\n"
            "  --exec \"cmd args\"       un programa externo y sale\n"
            "  --diff N                dificultad 0..100 (textual)\n"
            "  --sim S                 segundos de simulación\n"
            "  --timeout S             timeout (default 300)\n"
            "  --max-live N            cupo de procesos (default 64)\n"
            "  --max-depth N           profundidad máxima (default 8)\n"
            "  --experiment            no mata el árbol al vencer el timeout\n",
            argv0);
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    signal(SIGPIPE, SIG_IGN);
    srand((unsigned)time(NULL) ^ (unsigned)getpid());

    box_t box;
    memset(&box, 0, sizeof(box));
    box.timeout_sec = DEFAULT_TIMEOUT_SEC;
    box.diff_mode = 1;
    box.max_live = DEFAULT_MAX_LIVE;
    box.max_depth = DEFAULT_MAX_DEPTH;
    box.default_sim = 0;

    request_msg_t oneshot;
    memset(&oneshot, 0, sizeof(oneshot));
    int has_oneshot = 0;
    double cli_diff = -1;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--experiment") == 0) {
            box.experiment = 1;
        } else if (strcmp(argv[i], "--timeout") == 0 && i + 1 < argc) {
            box.timeout_sec = strtod(argv[++i], NULL);
        } else if (strcmp(argv[i], "--max-live") == 0 && i + 1 < argc) {
            box.max_live = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--max-depth") == 0 && i + 1 < argc) {
            box.max_depth = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--diff") == 0 && i + 1 < argc) {
            cli_diff = strtod(argv[++i], NULL);
            box.diff_mode = 1;
        } else if (strcmp(argv[i], "--sim") == 0 && i + 1 < argc) {
            box.default_sim = strtod(argv[++i], NULL);
        } else if (strcmp(argv[i], "--text") == 0 && i + 1 < argc) {
            oneshot.kind = REQ_TEXTUAL;
            snprintf(oneshot.text, sizeof(oneshot.text), "%s", argv[++i]);
            has_oneshot = 1;
        } else if (strcmp(argv[i], "--calc") == 0 && i + 1 < argc) {
            oneshot.kind = REQ_CALC;
            snprintf(oneshot.text, sizeof(oneshot.text), "%s", argv[++i]);
            has_oneshot = 1;
        } else if (strcmp(argv[i], "--exec") == 0 && i + 1 < argc) {
            oneshot.kind = REQ_EXEC;
            snprintf(oneshot.text, sizeof(oneshot.text), "%s", argv[++i]);
            has_oneshot = 1;
        } else {
            fprintf(stderr, "opción desconocida: %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    box.shared = shared_create();
    if (!box.shared)
        return 1;
    if (slots_open(&box, (unsigned)box.max_live) < 0) {
        shared_destroy(box.shared);
        return 1;
    }

    report_t rep;
    memset(&rep, 0, sizeof(rep));

    int rc = 0;
    if (has_oneshot) {
        oneshot.n = 1;
        oneshot.i = 1;
        oneshot.max_depth = box.max_depth;
        oneshot.max_live = box.max_live;
        oneshot.sim_seconds = box.default_sim;
        oneshot.task_id = 1;
        if (oneshot.kind == REQ_TEXTUAL)
            oneshot.difficulty = (cli_diff >= 0) ? cli_diff : 100.0;
        else
            oneshot.difficulty = 100.0;
        print_banner();
        rc = run_request(&box, &rep, &oneshot) ? 0 : 1;
        print_report(&rep, &box);
    } else {
        rc = interactive(&box, &rep);
    }

    slots_close(&box);
    shared_destroy(box.shared);
    return rc;
}
