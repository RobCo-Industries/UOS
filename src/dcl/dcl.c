/* DCL tokeniser, continuation, numbers, lookup and parser. Pure. */

#include "dcl.h"

#include <robco/klibc.h>

static const char *const punct_text[] = {
    [DCL_TOK_END] = "",
    [DCL_TOK_WORD] = "",
    [DCL_TOK_SLASH] = "/",
    [DCL_TOK_EQUALS] = "=",
    [DCL_TOK_LPAREN] = "(",
    [DCL_TOK_RPAREN] = ")",
    [DCL_TOK_COMMA] = ",",
};

static int is_blank(char c)
{
    return c == ' ' || c == '\t';
}

static char upper(char c)
{
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

static int is_printable(char c)
{
    return (unsigned char)c >= 0x20 && (unsigned char)c < 0x7F;
}

static int is_word_char(char c)
{
    return is_printable(c) && c != ' ' && c != '/' && c != '=' && c != '(' && c != ')' &&
           c != ',' && c != '!' && c != '"';
}

void dcl_lex_init(dcl_lexer_t *lx, const char *line, char *out, size_t out_size)
{
    lx->p = line;
    lx->out = out;
    lx->out_size = out_size;
    lx->out_len = 0;
}

dcl_msg_t dcl_lex_next(dcl_lexer_t *lx, dcl_token_t *tok)
{
    const char *p = lx->p;
    size_t start;
    size_t n = 0;

    tok->type = DCL_TOK_END;
    tok->quoted = 0;
    tok->text = NULL;
    while (is_blank(*p)) {
        p++;
    }
    lx->p = p;
    switch (*p) {
    case '\0':
    case '!':
        return DCL_NORMAL;
    case '/':
        tok->type = DCL_TOK_SLASH;
        break;
    case '=':
        tok->type = DCL_TOK_EQUALS;
        break;
    case '(':
        tok->type = DCL_TOK_LPAREN;
        break;
    case ')':
        tok->type = DCL_TOK_RPAREN;
        break;
    case ',':
        tok->type = DCL_TOK_COMMA;
        break;
    default:
        break;
    }
    if (tok->type != DCL_TOK_END) {
        lx->p = p + 1;
        return DCL_NORMAL;
    }
    if (*p != '"' && !is_word_char(*p)) {
        return DCL_IVCHAR;
    }
    start = lx->out_len;
    for (;;) {
        char c;

        if (*p == '"') {
            p++;
            for (;;) {
                if (*p == '\0') {
                    return DCL_UNTERM;
                }
                if (*p == '"') {
                    if (p[1] != '"') {
                        p++;
                        break;
                    }
                    p++;
                } else if (!is_printable(*p) && *p != '\t') {
                    return DCL_IVCHAR;
                }
                if (n >= DCL_TOKEN_MAX) {
                    return DCL_TKNOVF;
                }
                if (start + n + 1 >= lx->out_size) {
                    return DCL_LINOVF;
                }
                lx->out[start + n++] = *p++;
            }
            tok->quoted = 1;
            continue;
        }
        c = *p;
        if (!is_word_char(c)) {
            break;
        }
        if (n >= DCL_TOKEN_MAX) {
            return DCL_TKNOVF;
        }
        if (start + n + 1 >= lx->out_size) {
            return DCL_LINOVF;
        }
        lx->out[start + n++] = upper(c);
        p++;
    }
    if (*p != '\0' && !is_printable(*p) && !is_blank(*p)) {
        return DCL_IVCHAR;
    }
    if (start + n + 1 > lx->out_size) {
        return DCL_LINOVF;
    }
    lx->out[start + n] = '\0';
    lx->out_len = start + n + 1;
    lx->p = p;
    tok->type = DCL_TOK_WORD;
    tok->text = lx->out + start;
    return DCL_NORMAL;
}

void dcl_line_reset(dcl_line_t *ln)
{
    ln->len = 0;
    ln->text[0] = '\0';
}

dcl_msg_t dcl_line_add(dcl_line_t *ln, const char *piece)
{
    size_t i;
    size_t end = 0;      /* one past the last non-blank character outside a comment */
    int in_quote = 0;
    int last_quoted = 0;
    int more;
    size_t take;

    for (i = 0; piece[i] != '\0'; i++) {
        char c = piece[i];

        if (c == '"') {
            in_quote = !in_quote;
        } else if (c == '!' && !in_quote) {
            break;
        }
        if (!is_blank(c)) {
            end = i + 1;
            last_quoted = in_quote;
        }
    }
    while (piece[i] != '\0') {
        i++;
    }
    more = end > 0 && piece[end - 1] == '-' && !last_quoted;
    take = more ? end - 1 : i;
    if (ln->len + take > DCL_LINE_MAX) {
        dcl_line_reset(ln);
        return DCL_LINOVF;
    }
    k_memcpy(ln->text + ln->len, piece, take);
    ln->len += take;
    ln->text[ln->len] = '\0';
    return more ? DCL_CONTIN : DCL_NORMAL;
}

static int digit_value(char c)
{
    c = upper(c);
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return 99;
}

dcl_msg_t dcl_parse_number(const char *s, int32_t min, int32_t max, int32_t *out)
{
    int neg = 0;
    unsigned base = 10;
    uint32_t mag = 0;
    int big = 0;
    int digits = 0;
    int32_t v;

    if (s == NULL) {
        return DCL_IVNUMB;
    }
    if (*s == '-' || *s == '+') {
        neg = *s == '-';
        s++;
    }
    if (*s == '%') {
        char r = upper(s[1]);

        if (r == 'X') {
            base = 16;
        } else if (r == 'O') {
            base = 8;
        } else if (r == 'D') {
            base = 10;
        } else {
            return DCL_IVNUMB;
        }
        s += 2;
    }
    for (; *s != '\0'; s++) {
        int d = digit_value(*s);
        uint64_t next;

        if (d >= (int)base) {
            return DCL_IVNUMB;
        }
        digits++;
        next = (uint64_t)mag * base + (unsigned)d;
        if (next > 0x80000000u) {
            big = 1;
        } else {
            mag = (uint32_t)next;
        }
    }
    if (digits == 0) {
        return DCL_IVNUMB;
    }
    if (big || (!neg && mag > 0x7FFFFFFFu)) {
        return DCL_NUMRNG;
    }
    if (neg) {
        v = mag == 0x80000000u ? INT32_MIN : -(int32_t)mag;
    } else {
        v = (int32_t)mag;
    }
    if (v < min || v > max) {
        return DCL_NUMRNG;
    }
    *out = v;
    return DCL_NORMAL;
}

dcl_lookup_t dcl_lookup(const char *word, const void *items, size_t count, size_t stride,
                        size_t *index)
{
    size_t len = k_strlen(word);
    size_t i;
    size_t hits = 0;
    size_t hit = 0;

    if (len == 0 || items == NULL) {
        return DCL_LK_NONE;
    }
    for (i = 0; i < count; i++) {
        const char *name = *(const char *const *)(const void *)((const char *)items + i * stride);

        if (k_strcmp(name, word) == 0) {
            *index = i;
            return DCL_LK_FOUND;
        }
        if (k_strncmp(name, word, len) == 0) {
            hits++;
            hit = i;
        }
    }
    if (hits == 0) {
        return DCL_LK_NONE;
    }
    if (hits > 1) {
        return DCL_LK_AMBIG;
    }
    *index = hit;
    return DCL_LK_FOUND;
}

dcl_lookup_t dcl_lookup_key(const char *word, const char *const *keys, size_t *index)
{
    size_t n = 0;

    if (keys == NULL) {
        return DCL_LK_NONE;
    }
    while (keys[n] != NULL) {
        n++;
    }
    return dcl_lookup(word, keys, n, sizeof(keys[0]), index);
}

static int name_ok(const char *s)
{
    if (s == NULL || *s == '\0') {
        return 0;
    }
    for (; *s != '\0'; s++) {
        char c = *s;

        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '$' || c == '_')) {
            return 0;
        }
    }
    return 1;
}

static int names_check(const void *items, size_t count, size_t stride, const char **bad)
{
    size_t i;
    size_t j;

    for (i = 0; i < count; i++) {
        const char *a = *(const char *const *)(const void *)((const char *)items + i * stride);

        if (!name_ok(a)) {
            *bad = a;
            return -1;
        }
        for (j = 0; j < i; j++) {
            const char *b = *(const char *const *)(const void *)((const char *)items + j * stride);

            if (k_strncmp(a, b, DCL_ABBREV_SURE) == 0) {
                *bad = a;
                return -1;
            }
        }
    }
    return 0;
}

static int keys_check(const char *const *keys, const char **bad)
{
    size_t n = 0;

    if (keys == NULL) {
        return -1;
    }
    while (keys[n] != NULL) {
        n++;
    }
    return n == 0 ? -1 : names_check(keys, n, sizeof(keys[0]), bad);
}

static int verb_check(const dcl_verb_t *v, int level, const char **bad)
{
    size_t i;

    *bad = v->name;
    if (v->nreq > v->nmax || v->nmax > DCL_PARAM_MAX || v->nquals > DCL_QUAL_MAX ||
        (v->nmax > 0 && v->params == NULL) || (v->nquals > 0 && v->quals == NULL)) {
        return -1;
    }
    for (i = 0; i < v->nmax; i++) {
        const dcl_param_t *p = &v->params[i];

        if (p->prompt == NULL || p->type == DCL_VT_NONE) {
            return -1;
        }
        if (p->type == DCL_VT_KEYWORD && keys_check(p->keys, bad) != 0) {
            return -1;
        }
        if (p->type == DCL_VT_NUMBER && p->min > p->max) {
            return -1;
        }
        *bad = v->name;
    }
    if (names_check(v->quals, v->nquals, sizeof(v->quals[0]), bad) != 0) {
        return -1;
    }
    for (i = 0; i < v->nquals; i++) {
        const dcl_qual_t *q = &v->quals[i];

        *bad = q->name;
        if ((q->value == DCL_QV_NONE) != (q->type == DCL_VT_NONE)) {
            return -1;
        }
        if ((q->flags & DCL_QF_LIST) && q->value == DCL_QV_NONE) {
            return -1;
        }
        if (q->type == DCL_VT_KEYWORD && keys_check(q->keys, bad) != 0) {
            return -1;
        }
    }
    if (v->nkeys > 0) {
        if (level > 0 || v->keys == NULL || v->key_prompt == NULL || v->nmax != 0) {
            *bad = v->name;
            return -1;
        }
        if (names_check(v->keys, v->nkeys, sizeof(v->keys[0]), bad) != 0) {
            return -1;
        }
        for (i = 0; i < v->nkeys; i++) {
            if (verb_check(&v->keys[i], 1, bad) != 0) {
                return -1;
            }
        }
    }
    if (level == 0 && v->help == NULL) {
        *bad = v->name;
        return -1;
    }
    return 0;
}

int dcl_table_check(const dcl_table_t *table, const char **bad)
{
    size_t i;

    *bad = NULL;
    if (names_check(table->verbs, table->count, sizeof(table->verbs[0]), bad) != 0) {
        return -1;
    }
    for (i = 0; i < table->count; i++) {
        if (verb_check(&table->verbs[i], 0, bad) != 0) {
            return -1;
        }
    }
    *bad = NULL;
    return 0;
}

static dcl_msg_t fail(dcl_cmd_t *cmd, dcl_msg_t m, const char *token)
{
    cmd->token = token;
    return m;
}

static dcl_msg_t check_value(dcl_cmd_t *cmd, dcl_vtype_t type, const char *const *keys,
                             int32_t min, int32_t max, const char **value, int32_t *num)
{
    size_t k;
    dcl_lookup_t lk;
    dcl_msg_t m;

    switch (type) {
    case DCL_VT_KEYWORD:
        lk = dcl_lookup_key(*value, keys, &k);
        if (lk != DCL_LK_FOUND) {
            return fail(cmd, lk == DCL_LK_AMBIG ? DCL_ABKEYW : DCL_IVKEYW, *value);
        }
        *value = keys[k];
        return DCL_NORMAL;
    case DCL_VT_NUMBER:
        m = dcl_parse_number(*value, min, max, num);
        return m == DCL_NORMAL ? m : fail(cmd, m, *value);
    default:
        return DCL_NORMAL;
    }
}

static dcl_lookup_t find_qual(const dcl_verb_t *v, const char *word, const dcl_qual_t **q,
                              uint8_t *neg)
{
    size_t i;
    dcl_lookup_t lk = dcl_lookup(word, v->quals, v->nquals, sizeof(v->quals[0]), &i);
    dcl_lookup_t nlk;

    *neg = 0;
    if (lk == DCL_LK_FOUND) {
        *q = &v->quals[i];
        return lk;
    }
    if (word[0] == 'N' && word[1] == 'O' && word[2] != '\0') {
        nlk = dcl_lookup(word + 2, v->quals, v->nquals, sizeof(v->quals[0]), &i);
        if (nlk == DCL_LK_FOUND) {
            *q = &v->quals[i];
            *neg = 1;
            return nlk;
        }
    }
    return lk;
}

static dcl_msg_t resolve_qual(dcl_cmd_t *cmd, dcl_qval_t *qv, int has_value, int is_list)
{
    const dcl_qual_t *q = NULL;
    uint8_t neg = 0;
    dcl_lookup_t lk = DCL_LK_NONE;
    uint8_t i;
    dcl_msg_t m;

    if (cmd->keyword != NULL) {
        lk = find_qual(cmd->keyword, qv->typed, &q, &neg);
    }
    if (lk == DCL_LK_NONE) {
        lk = find_qual(cmd->verb, qv->typed, &q, &neg);
    }
    if (lk != DCL_LK_FOUND) {
        return fail(cmd, lk == DCL_LK_AMBIG ? DCL_ABQUAL : DCL_IVQUAL, qv->typed);
    }
    if (neg && !(q->flags & DCL_QF_NEG)) {
        return fail(cmd, DCL_NONEG, qv->typed);
    }
    if (has_value && (neg || q->value == DCL_QV_NONE)) {
        return fail(cmd, DCL_NOVALU, qv->typed);
    }
    if (!has_value && !neg && q->value == DCL_QV_REQUIRED) {
        return fail(cmd, DCL_VALREQ, qv->typed);
    }
    if (is_list && !(q->flags & DCL_QF_LIST)) {
        return fail(cmd, DCL_NOLIST, qv->typed);
    }
    qv->def = q;
    qv->negated = neg;
    for (i = 0; i < qv->nvalues; i++) {
        m = check_value(cmd, q->type, q->keys, q->min, q->max, &qv->values[i], &qv->numbers[i]);
        if (m != DCL_NORMAL) {
            return m;
        }
    }
    return DCL_NORMAL;
}

dcl_msg_t dcl_parse(const char *line, const dcl_table_t *table, dcl_cmd_t *cmd)
{
    dcl_lexer_t lx;
    dcl_token_t tok;
    dcl_msg_t m;
    const char *pos[DCL_PARAM_MAX + 1];
    size_t npos = 0;
    uint8_t has_value[DCL_QUAL_MAX];
    uint8_t is_list[DCL_QUAL_MAX];
    size_t i;
    size_t first;
    size_t nq = 0;
    const dcl_verb_t *target;
    dcl_lookup_t lk;
    int have_tok = 0;

    k_memset(cmd, 0, sizeof(*cmd));
    if (k_strlen(line) > DCL_LINE_MAX) {
        return DCL_LINOVF;
    }
    dcl_lex_init(&lx, line, cmd->text, sizeof(cmd->text));
    m = dcl_lex_next(&lx, &tok);
    if (m != DCL_NORMAL) {
        return m;
    }
    if (tok.type == DCL_TOK_END) {
        return DCL_NORMAL;
    }
    if (tok.type != DCL_TOK_WORD) {
        return fail(cmd, DCL_IVVERB, punct_text[tok.type]);
    }
    if (tok.quoted) {
        return fail(cmd, DCL_IVVERB, tok.text);
    }
    lk = dcl_lookup(tok.text, table->verbs, table->count, sizeof(table->verbs[0]), &i);
    if (lk != DCL_LK_FOUND) {
        return fail(cmd, lk == DCL_LK_AMBIG ? DCL_ABVERB : DCL_IVVERB, tok.text);
    }
    cmd->verb = &table->verbs[i];

    for (;;) {
        dcl_qval_t *qv;

        if (!have_tok) {
            m = dcl_lex_next(&lx, &tok);
            if (m != DCL_NORMAL) {
                return m;
            }
        }
        have_tok = 0;
        if (tok.type == DCL_TOK_END) {
            break;
        }
        if (tok.type == DCL_TOK_WORD) {
            size_t limit = DCL_PARAM_MAX + (cmd->verb->nkeys > 0 ? 1 : 0);

            if (npos >= limit) {
                return fail(cmd, DCL_MAXPARM, tok.text);
            }
            pos[npos++] = tok.text;
            continue;
        }
        if (tok.type != DCL_TOK_SLASH) {
            return fail(cmd, DCL_IVCHAR, punct_text[tok.type]);
        }
        m = dcl_lex_next(&lx, &tok);
        if (m != DCL_NORMAL) {
            return m;
        }
        if (tok.type != DCL_TOK_WORD || tok.quoted) {
            return fail(cmd, DCL_IVQUAL, tok.type == DCL_TOK_WORD ? tok.text : "/");
        }
        if (nq >= DCL_QUAL_MAX) {
            return fail(cmd, DCL_MAXQUAL, tok.text);
        }
        qv = &cmd->quals[nq];
        qv->typed = tok.text;
        has_value[nq] = 0;
        is_list[nq] = 0;
        nq++;
        m = dcl_lex_next(&lx, &tok);
        if (m != DCL_NORMAL) {
            return m;
        }
        if (tok.type != DCL_TOK_EQUALS) {
            have_tok = 1;
            continue;
        }
        has_value[nq - 1] = 1;
        m = dcl_lex_next(&lx, &tok);
        if (m != DCL_NORMAL) {
            return m;
        }
        if (tok.type == DCL_TOK_WORD) {
            qv->values[0] = tok.text;
            qv->nvalues = 1;
            continue;
        }
        if (tok.type != DCL_TOK_LPAREN) {
            return fail(cmd, DCL_VALREQ, qv->typed);
        }
        is_list[nq - 1] = 1;
        for (;;) {
            m = dcl_lex_next(&lx, &tok);
            if (m != DCL_NORMAL) {
                return m;
            }
            if (tok.type == DCL_TOK_END) {
                return fail(cmd, DCL_PARENS, qv->typed);
            }
            if (tok.type != DCL_TOK_WORD) {
                return fail(cmd, DCL_VALREQ, qv->typed);
            }
            if (qv->nvalues >= DCL_LIST_MAX) {
                return fail(cmd, DCL_MAXVAL, tok.text);
            }
            qv->values[qv->nvalues++] = tok.text;
            m = dcl_lex_next(&lx, &tok);
            if (m != DCL_NORMAL) {
                return m;
            }
            if (tok.type == DCL_TOK_RPAREN) {
                break;
            }
            if (tok.type == DCL_TOK_END) {
                return fail(cmd, DCL_PARENS, qv->typed);
            }
            if (tok.type != DCL_TOK_COMMA) {
                return fail(cmd, DCL_IVCHAR, tok.type == DCL_TOK_WORD ? tok.text
                                                                    : punct_text[tok.type]);
            }
        }
    }

    first = 0;
    if (cmd->verb->nkeys > 0) {
        if (npos == 0) {
            cmd->prompt = cmd->verb->key_prompt;
            return DCL_INSFPRM;
        }
        lk = dcl_lookup(pos[0], cmd->verb->keys, cmd->verb->nkeys, sizeof(cmd->verb->keys[0]), &i);
        if (lk != DCL_LK_FOUND) {
            return fail(cmd, lk == DCL_LK_AMBIG ? DCL_ABKEYW : DCL_IVKEYW, pos[0]);
        }
        cmd->keyword = &cmd->verb->keys[i];
        first = 1;
    }
    target = cmd->keyword != NULL ? cmd->keyword : cmd->verb;

    for (i = 0; i < nq; i++) {
        m = resolve_qual(cmd, &cmd->quals[i], has_value[i], is_list[i]);
        if (m != DCL_NORMAL) {
            return m;
        }
    }
    /* A qualifier given twice: the last one wins. */
    cmd->nquals = 0;
    for (i = 0; i < nq; i++) {
        size_t j;
        size_t at = cmd->nquals;

        for (j = 0; j < cmd->nquals; j++) {
            if (cmd->quals[j].def == cmd->quals[i].def) {
                at = j;
                break;
            }
        }
        if (at != i) {
            cmd->quals[at] = cmd->quals[i];
        }
        if (at == cmd->nquals) {
            cmd->nquals++;
        }
    }

    if (npos - first > target->nmax) {
        return fail(cmd, DCL_MAXPARM, pos[first + target->nmax]);
    }
    for (i = first; i < npos; i++) {
        const dcl_param_t *p = &target->params[cmd->nparams];

        cmd->params[cmd->nparams] = pos[i];
        m = check_value(cmd, p->type, p->keys, p->min, p->max, &cmd->params[cmd->nparams],
                        &cmd->numbers[cmd->nparams]);
        if (m != DCL_NORMAL) {
            return m;
        }
        cmd->nparams++;
    }
    if (cmd->nparams < target->nreq) {
        cmd->prompt = target->params[cmd->nparams].prompt;
        return DCL_INSFPRM;
    }
    return DCL_NORMAL;
}

static const dcl_qual_t *qual_def(const dcl_cmd_t *cmd, const char *name)
{
    const dcl_verb_t *sets[2] = { cmd->keyword, cmd->verb };
    size_t s;
    size_t i;

    for (s = 0; s < 2; s++) {
        if (sets[s] == NULL) {
            continue;
        }
        for (i = 0; i < sets[s]->nquals; i++) {
            if (k_strcmp(sets[s]->quals[i].name, name) == 0) {
                return &sets[s]->quals[i];
            }
        }
    }
    return NULL;
}

const dcl_qval_t *dcl_cmd_qual(const dcl_cmd_t *cmd, const char *name)
{
    size_t i;

    for (i = 0; i < cmd->nquals; i++) {
        if (cmd->quals[i].def != NULL && k_strcmp(cmd->quals[i].def->name, name) == 0) {
            return &cmd->quals[i];
        }
    }
    return NULL;
}

int dcl_cmd_on(const dcl_cmd_t *cmd, const char *name)
{
    const dcl_qval_t *qv = dcl_cmd_qual(cmd, name);
    const dcl_qual_t *q;

    if (qv != NULL) {
        return !qv->negated;
    }
    q = qual_def(cmd, name);
    return q != NULL && (q->flags & DCL_QF_DEFON) != 0;
}

const char *dcl_cmd_value(const dcl_cmd_t *cmd, const char *name)
{
    const dcl_qval_t *qv = dcl_cmd_qual(cmd, name);
    const dcl_qual_t *q;

    if (qv != NULL && qv->nvalues > 0) {
        return qv->values[0];
    }
    if (!dcl_cmd_on(cmd, name)) {
        return NULL;
    }
    q = qv != NULL ? qv->def : qual_def(cmd, name);
    return q != NULL ? q->def : NULL;
}

uint32_t dcl_cmd_privs(const dcl_cmd_t *cmd)
{
    uint32_t p = 0;

    if (cmd->verb != NULL) {
        p |= cmd->verb->privs;
    }
    if (cmd->keyword != NULL) {
        p |= cmd->keyword->privs;
    }
    return p;
}

dcl_msg_t dcl_check_privs(const dcl_cmd_t *cmd, uint32_t have)
{
    uint32_t need = dcl_cmd_privs(cmd);

    if ((have & DCL_PRIV_SYSTEM) || (need & ~have) == 0) {
        return DCL_NORMAL;
    }
    return DCL_SYS_NOPRIV;
}
