/*
 * Evaluador sencillo de expresiones aritméticas y lógicas.
 * Un solo Mr. Meeseeks lo ejecuta (apartado B).
 *
 * Aritmética: + - * / % y paréntesis, enteros long.
 * Lógica:     !  &&  ||  == != < > <= >=  (0 falso, distinto de 0 verdadero)
 *
 * Daniel Mendez Romero — 2022437806
 */

#include "meeseeks_box.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *p;
    int         fail;
    char        err[128];
} parser_t;

static void skip(parser_t *ps)
{
    while (*ps->p && isspace((unsigned char)*ps->p))
        ps->p++;
}

static void seterr(parser_t *ps, const char *m)
{
    if (!ps->fail) {
        ps->fail = 1;
        snprintf(ps->err, sizeof(ps->err), "%s", m);
    }
}

static long parse_or(parser_t *ps);

static int match(parser_t *ps, const char *tok)
{
    skip(ps);
    size_t n = strlen(tok);
    if (strncmp(ps->p, tok, n) == 0) {
        /* Evitar que "==" coincida con un "=" suelto ya cubierto por tok. */
        ps->p += n;
        return 1;
    }
    return 0;
}

static long parse_primary(parser_t *ps)
{
    skip(ps);
    if (*ps->p == '(') {
        ps->p++;
        long v = parse_or(ps);
        skip(ps);
        if (*ps->p != ')') {
            seterr(ps, "falta ')'");
            return 0;
        }
        ps->p++;
        return v;
    }
    if (isdigit((unsigned char)*ps->p) ||
        ((*ps->p == '-' || *ps->p == '+') && isdigit((unsigned char)ps->p[1]))) {
        char *end = NULL;
        long v = strtol(ps->p, &end, 10);
        if (end == ps->p) {
            seterr(ps, "número inválido");
            return 0;
        }
        ps->p = end;
        return v;
    }
    seterr(ps, "se esperaba número o '('");
    return 0;
}

static long parse_unary(parser_t *ps)
{
    skip(ps);
    if (*ps->p == '!') {
        ps->p++;
        return !parse_unary(ps);
    }
    if (*ps->p == '-' && !isdigit((unsigned char)ps->p[1])) {
        ps->p++;
        return -parse_unary(ps);
    }
    if (*ps->p == '+') {
        ps->p++;
        return parse_unary(ps);
    }
    return parse_primary(ps);
}

static long parse_mul(parser_t *ps)
{
    long v = parse_unary(ps);
    for (;;) {
        skip(ps);
        if (*ps->p == '*') {
            ps->p++;
            v *= parse_unary(ps);
        } else if (*ps->p == '/') {
            ps->p++;
            long r = parse_unary(ps);
            if (r == 0) {
                seterr(ps, "división por cero");
                return 0;
            }
            v /= r;
        } else if (*ps->p == '%') {
            ps->p++;
            long r = parse_unary(ps);
            if (r == 0) {
                seterr(ps, "módulo por cero");
                return 0;
            }
            v %= r;
        } else {
            break;
        }
        if (ps->fail)
            return 0;
    }
    return v;
}

static long parse_add(parser_t *ps)
{
    long v = parse_mul(ps);
    for (;;) {
        skip(ps);
        if (*ps->p == '+' ) {
            ps->p++;
            v += parse_mul(ps);
        } else if (*ps->p == '-') {
            ps->p++;
            v -= parse_mul(ps);
        } else {
            break;
        }
        if (ps->fail)
            return 0;
    }
    return v;
}

static long parse_cmp(parser_t *ps)
{
    long v = parse_add(ps);
    skip(ps);
    if (match(ps, "=="))
        return v == parse_add(ps);
    if (match(ps, "!="))
        return v != parse_add(ps);
    if (match(ps, "<="))
        return v <= parse_add(ps);
    if (match(ps, ">="))
        return v >= parse_add(ps);
    if (*ps->p == '<' && ps->p[1] != '<') {
        ps->p++;
        return v < parse_add(ps);
    }
    if (*ps->p == '>' && ps->p[1] != '>') {
        ps->p++;
        return v > parse_add(ps);
    }
    return v;
}

static long parse_and(parser_t *ps)
{
    long v = parse_cmp(ps);
    while (!ps->fail) {
        skip(ps);
        if (!match(ps, "&&"))
            break;
        long r = parse_cmp(ps);
        v = (v && r) ? 1 : 0;
    }
    return v;
}

static long parse_or(parser_t *ps)
{
    long v = parse_and(ps);
    while (!ps->fail) {
        skip(ps);
        if (!match(ps, "||"))
            break;
        long r = parse_and(ps);
        v = (v || r) ? 1 : 0;
    }
    return v;
}

int calc_eval(const char *expr, long *out, char *err, size_t errlen)
{
    parser_t ps;
    memset(&ps, 0, sizeof(ps));
    ps.p = expr ? expr : "";
    long v = parse_or(&ps);
    skip(&ps);
    if (!ps.fail && *ps.p != '\0')
        seterr(&ps, "caracteres extra al final");
    if (ps.fail) {
        if (err && errlen)
            snprintf(err, errlen, "%s", ps.err);
        return -1;
    }
    *out = v;
    return 0;
}
