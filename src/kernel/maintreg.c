/*
 * The maintenance command table and the word splitting. Pure: no
 * hardware, the table lives in a struct the caller owns.
 */

#include <robco/klibc.h>

#include "uos_maint.h"

/* The script channel answers these itself: a handler never sees them. */
static const char *const builtins[] = { "key", "char", "text", "dial", "wait", "stats" };

static int name_ok(const char *name)
{
    size_t n;

    if (name == NULL) {
        return 0;
    }
    for (n = 0; name[n] != '\0'; n++) {
        if (n >= UOS_MAINT_NAME_MAX || name[n] < 'a' || name[n] > 'z') {
            return 0;
        }
    }
    return n > 0;
}

void uos_mt_init(uos_maint_table_t *t)
{
    k_memset(t, 0, sizeof(*t));
}

const uos_maint_cmd_t *uos_mt_find(const uos_maint_table_t *t, const char *name)
{
    unsigned i;

    if (name == NULL) {
        return NULL;
    }
    for (i = 0; i < t->count; i++) {
        if (k_strcmp(t->cmd[i].name, name) == 0) {
            return &t->cmd[i];
        }
    }
    return NULL;
}

int uos_mt_add(uos_maint_table_t *t, const char *name, uos_maint_fn fn, const char *help)
{
    unsigned i, at;

    if (!name_ok(name)) {
        return UOS_MAINT_ERR_NAME;
    }
    if (fn == NULL) {
        return UOS_MAINT_ERR_ARG;
    }
    for (i = 0; i < sizeof(builtins) / sizeof(builtins[0]); i++) {
        if (k_strcmp(builtins[i], name) == 0) {
            return UOS_MAINT_ERR_DUP;
        }
    }
    if (uos_mt_find(t, name) != NULL) {
        return UOS_MAINT_ERR_DUP;
    }
    if (t->count >= UOS_MAINT_MAX) {
        return UOS_MAINT_ERR_FULL;
    }
    for (at = 0; at < t->count && k_strcmp(t->cmd[at].name, name) < 0; at++) {
    }
    for (i = t->count; i > at; i--) {
        t->cmd[i] = t->cmd[i - 1];
    }
    k_memset(&t->cmd[at], 0, sizeof(t->cmd[at]));
    k_strncpy(t->cmd[at].name, name, UOS_MAINT_NAME_MAX);
    t->cmd[at].fn = fn;
    t->cmd[at].help = help != NULL ? help : "";
    t->count++;
    return 0;
}

static int blank(char c)
{
    return c == ' ' || c == '\t';
}

int uos_mt_split(char *line, char **argv, unsigned max)
{
    char *r = line, *w;
    unsigned argc = 0;

    if (k_strlen(line) > UOS_MAINT_LINE_MAX) {
        return UOS_MAINT_SPLIT_LONG;
    }
    for (;;) {
        while (blank(*r)) {
            r++;
        }
        if (*r == '\0') {
            break;
        }
        if (argc == max) {
            return UOS_MAINT_SPLIT_WORDS;
        }
        /* Words are written back in place; the output never passes the input. */
        w = r;
        argv[argc++] = w;
        if (*r == '"') {
            r++;
            for (;;) {
                if (*r == '\0') {
                    return UOS_MAINT_SPLIT_QUOTE;
                }
                if (*r == '"') {
                    if (r[1] == '"') {
                        *w++ = '"';
                        r += 2;
                        continue;
                    }
                    r++;
                    break;
                }
                *w++ = *r++;
            }
            if (*r != '\0' && !blank(*r)) {
                return UOS_MAINT_SPLIT_QUOTE;
            }
        } else {
            while (*r != '\0' && !blank(*r)) {
                *w++ = *r++;
            }
        }
        if (*r != '\0') {
            r++;
        }
        *w = '\0';
    }
    argv[argc] = NULL;
    return (int)argc;
}

char *uos_mt_rejoin(char *first, char *last)
{
    char *p;

    for (p = first; p < last; p++) {
        if (*p == '\0') {
            *p = ' ';
        }
    }
    return first;
}
