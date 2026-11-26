/* The shell's rules. Pure. See shrules.h. */

#include "shrules.h"

#include <robco/klibc.h>

#include "path.h"
#include "uaf.h"
#include "vfs.h"

/* ---- Errors ---- */

static dcl_msg_t dev_msg(int code)
{
    switch (code) {
    case VFS_E_NODEV:     return DCL_SYS_NODEV;
    case VFS_E_NOPRIV:    return DCL_SYS_NOPRIV;
    case VFS_E_READONLY:  return DCL_SYS_SENSOR;
    case VFS_E_ARG:       return DCL_SYS_IVACT;
    case VFS_E_WRITE:
    case VFS_E_READ:      return DCL_SYS_DEVERR;
    default:              return vfs_msg(code);
    }
}

static dcl_msg_t uaf_msg(int code)
{
    switch (code) {
    case UAF_E_NO_USER:       return DCL_SYS_NOUSER;
    case UAF_E_EXISTS:        return DCL_SYS_UEXISTS;
    case UAF_E_FULL:          return DCL_SYS_UAFFULL;
    case UAF_E_NAME:          return DCL_SYS_IVUSER;
    case UAF_E_HOME:          return DCL_SYS_IVHOME;
    case UAF_E_PASSWORD_LEN:
    case UAF_E_PASSWORD_CHAR: return DCL_SYS_IVPWD;
    case UAF_E_BAD_LOGIN:
    case UAF_E_DISABLED:      return DCL_SYS_BADPWD;
    case UAF_E_PRIV:          return DCL_IVKEYW;
    default:                  return DCL_SYS_UAFERR;
    }
}

dcl_msg_t sh_err_msg(int src, int code, int vp, uint32_t refused)
{
    if (code == 0) {
        return DCL_NORMAL;
    }
    if (src == SH_SRC_UAF) {
        return uaf_msg(code);
    }
    if (src == SH_SRC_VP) {
        switch (code) {
        case VP_E_NODEV:     return DCL_SYS_NODEV;
        case VP_E_NOPRIV:    return DCL_SYS_NOPRIV;
        case VP_E_READONLY:  return DCL_SYS_WRITLCK;
        case VP_E_PROTECTED: return DCL_SYS_PROTECT;
        default:             return DCL_SYS_IVSPEC;
        }
    }
    if (code == VFS_E_NOFILES && refused > 0) {
        return vp == VP_E_PROTECTED ? DCL_SYS_PROTECT : DCL_SYS_NOPRIV;
    }
    if (src == SH_SRC_DEV) {
        return dev_msg(code);
    }
    return vfs_msg(code);
}

/* ---- Wildcard file loops ---- */

static char up(char c)
{
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

/* Compares the trimmed answer with `word`, case-insensitive. */
static int is_word(const char *s, size_t n, const char *word)
{
    size_t i;

    if (k_strlen(word) != n) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        if (up(s[i]) != word[i]) {
            return 0;
        }
    }
    return 1;
}

int sh_confirm_answer(const char *text)
{
    static const char *const yes[] = { "Y", "YES", "T", "TRUE", "1" };
    static const char *const no[] = { "N", "NO", "F", "FALSE", "0" };
    static const char *const quit[] = { "Q", "QUIT" };
    static const char *const all[] = { "A", "ALL" };
    size_t a = 0;
    size_t b;
    size_t i;

    if (text == NULL) {
        return SH_ANS_NO;
    }
    b = k_strlen(text);
    while (a < b && (text[a] == ' ' || text[a] == '\t')) {
        a++;
    }
    while (b > a && (text[b - 1] == ' ' || text[b - 1] == '\t')) {
        b--;
    }
    if (a == b) {
        return SH_ANS_NO;
    }
    for (i = 0; i < sizeof(yes) / sizeof(yes[0]); i++) {
        if (is_word(text + a, b - a, yes[i])) {
            return SH_ANS_YES;
        }
    }
    for (i = 0; i < sizeof(no) / sizeof(no[0]); i++) {
        if (is_word(text + a, b - a, no[i])) {
            return SH_ANS_NO;
        }
    }
    for (i = 0; i < 2; i++) {
        if (is_word(text + a, b - a, quit[i])) {
            return SH_ANS_QUIT;
        }
        if (is_word(text + a, b - a, all[i])) {
            return SH_ANS_ALL;
        }
    }
    return SH_ANS_AGAIN;
}

int sh_confirm_step(int answer, uint8_t *all)
{
    switch (answer) {
    case SH_ANS_YES:  return SH_DO_ACT;
    case SH_ANS_NO:   return SH_DO_SKIP;
    case SH_ANS_QUIT: return SH_DO_STOP;
    case SH_ANS_ALL:
        if (all != NULL) {
            *all = 1;
        }
        return SH_DO_ACT;
    default:          return SH_DO_ASK;
    }
}

int sh_confirm_need(int confirm, uint8_t all)
{
    return (confirm && !all) ? SH_DO_ASK : SH_DO_ACT;
}

void sh_tally_init(sh_tally_t *t)
{
    k_memset(t, 0, sizeof(*t));
    t->last_err = DCL_NORMAL;
}

dcl_msg_t sh_tally_result(const sh_tally_t *t, dcl_msg_t ok, uint32_t *num)
{
    *num = t->done;
    if (t->cancelled && t->done == 0) {
        return DCL_SYS_CANCEL;
    }
    if (t->done > 0 || (t->failed == 0 && t->refused == 0)) {
        return ok;
    }
    if (t->failed > 0) {
        return t->last_err;
    }
    return DCL_SYS_NOPRIV;
}

/* ---- Text ---- */

static int hexval(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    c = up(c);
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

int sh_unquote(const char *in, char *out, size_t size)
{
    size_t n;
    size_t o = 0;
    size_t i;

    if (out == NULL || size == 0) {
        return -1;
    }
    out[0] = '\0';
    if (in == NULL) {
        return -1;
    }
    n = k_strlen(in);
    if (n >= 2 && in[0] == '"' && in[n - 1] == '"') {
        in++;
        n -= 2;
    }
    for (i = 0; i < n; i++) {
        char c = in[i];

        if (c == '%') {
            int h;
            int l;

            if (i + 2 >= n) {
                out[0] = '\0';
                return -1;
            }
            h = hexval(in[i + 1]);
            l = hexval(in[i + 2]);
            if (h < 0 || l < 0) {
                out[0] = '\0';
                return -1;
            }
            c = (char)(h * 16 + l);
            i += 2;
        }
        if (o + 1 >= size) {
            out[0] = '\0';
            return -1;
        }
        out[o++] = c;
    }
    out[o] = '\0';
    return (int)o;
}

const char *sh_spec_tail(const char *spec)
{
    const char *t = spec;
    const char *p;

    if (spec == NULL) {
        return "";
    }
    for (p = spec; *p != '\0'; p++) {
        if (*p == ']' || *p == ':') {
            t = p + 1;
        }
    }
    return t;
}

int sh_next_line(const char *text, size_t size, size_t *pos, size_t *start, size_t *len)
{
    size_t p = *pos;
    size_t e;

    if (text == NULL || p >= size) {
        return 0;
    }
    e = p;
    while (e < size && text[e] != '\n' && text[e] != '\r') {
        e++;
    }
    *start = p;
    *len = e - p;
    if (e < size && text[e] == '\r') {
        e++;
    }
    if (e < size && text[e] == '\n') {
        e++;
    }
    *pos = e;
    return 1;
}

int sh_next_row(const char *text, size_t size, size_t width, size_t *pos, size_t *start,
                size_t *len)
{
    size_t p = *pos;
    size_t e;
    size_t lim;

    if (text == NULL || p >= size) {
        return 0;
    }
    if (width == 0) {
        width = 1;
    }
    lim = size - p > width ? p + width : size;
    e = p;
    while (e < lim && text[e] != '\n' && text[e] != '\r') {
        e++;
    }
    *start = p;
    *len = e - p;
    if (e == lim && e < size && text[e] != '\n' && text[e] != '\r') {
        /* The line goes on: this row is full, the rest follows. */
        *pos = e;
        return 1;
    }
    if (e < size && text[e] == '\r') {
        e++;
    }
    if (e < size && text[e] == '\n') {
        e++;
    }
    *pos = e;
    return 1;
}

int sh_output_write(dcl_msg_t final, int cancelled)
{
    char sev = dcl_msg_severity(final);

    if (cancelled || final == DCL_SYS_CANCEL) {
        return 0;
    }
    return sev == 'S' || sev == 'I';
}

int sh_tag_live(int active, uint32_t current, uint32_t tag)
{
    return active && tag != 0 && current == tag;
}

int sh_job_waits(int shell_on, int shell_on_top)
{
    return shell_on && !shell_on_top;
}

int sh_takes_job(int shell_on, int shell_on_top)
{
    return shell_on && shell_on_top;
}

int sh_owe(int *owed, int what)
{
    int first = *owed == SH_OWE_NONE;

    if (what > *owed) {
        *owed = what;
    }
    return first;
}

int sh_owe_take(int *owed)
{
    int what = *owed;

    *owed = SH_OWE_NONE;
    return what;
}

static int fit(int n, size_t size)
{
    return (n < 0 || (size_t)n >= size) ? -1 : n;
}

int sh_fmt_uptime(uint32_t seconds, char *buf, size_t size)
{
    uint32_t d = seconds / 86400u;
    uint32_t r = seconds % 86400u;
    int n;

    if (buf == NULL || size == 0) {
        return -1;
    }
    n = k_snprintf(buf, size, "%u %02u:%02u:%02u", (unsigned)d, (unsigned)(r / 3600u),
                   (unsigned)(r % 3600u / 60u), (unsigned)(r % 60u));
    n = fit(n, size);
    if (n < 0) {
        buf[0] = '\0';
    }
    return n;
}

int sh_fmt_datetime(unsigned year, unsigned month, unsigned day, unsigned hour,
                    unsigned minute, unsigned second, char *buf, size_t size)
{
    static const char *const mon[12] = { "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                         "JUL", "AUG", "SEP", "OCT", "NOV", "DEC" };
    int n;

    if (buf == NULL || size == 0) {
        return -1;
    }
    buf[0] = '\0';
    if (month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 || minute > 59 ||
        second > 59 || year > 9999) {
        return -1;
    }
    n = k_snprintf(buf, size, "%02u-%s-%04u %02u:%02u:%02u", day, mon[month - 1], year, hour,
                   minute, second);
    n = fit(n, size);
    if (n < 0) {
        buf[0] = '\0';
    }
    return n;
}

int sh_fmt_size(uint64_t bytes, char *buf, size_t size)
{
    uint32_t v;
    const char *unit;
    int n;

    if (buf == NULL || size == 0) {
        return -1;
    }
    if (bytes < 10240u) {
        v = (uint32_t)bytes;
        unit = "";
    } else if (bytes < (uint64_t)10240u * 1024u) {
        v = (uint32_t)(bytes >> 10);
        unit = " KiB";
    } else if (bytes < (uint64_t)10240u * 1024u * 1024u) {
        v = (uint32_t)(bytes >> 20);
        unit = " MiB";
    } else {
        v = (uint32_t)(bytes >> 30);
        unit = " GiB";
    }
    n = fit(k_snprintf(buf, size, "%u%s", (unsigned)v, unit), size);
    if (n < 0) {
        buf[0] = '\0';
    }
    return n;
}

int sh_dir_line(const char *name, const char *type, int is_dir, uint32_t bytes, const char *date,
                unsigned cols, char *buf, size_t size)
{
    char file[VP_NAME_MAX + 1 + VP_TYPE_MAX + 1];
    char sz[16];
    int n;

    if (buf == NULL || size == 0 || name == NULL || type == NULL) {
        return -1;
    }
    buf[0] = '\0';
    if (fit(k_snprintf(file, sizeof(file), "%s.%s", name, is_dir ? "DIR" : type),
            sizeof(file)) < 0) {
        return -1;
    }
    sz[0] = '\0';
    if ((cols & SH_DIR_SIZE) && !is_dir) {
        (void)k_snprintf(sz, sizeof(sz), "%u", (unsigned)bytes);
    }
    if (cols & SH_DIR_DATE) {
        n = k_snprintf(buf, size, "%-13s%10s  %s", file, sz, date != NULL ? date : "");
    } else if (cols & SH_DIR_SIZE) {
        n = k_snprintf(buf, size, "%-13s%10s", file, sz);
    } else {
        n = k_snprintf(buf, size, "%s", file);
    }
    n = fit(n, size);
    if (n < 0) {
        buf[0] = '\0';
        return -1;
    }
    /* No trailing blanks: a folder with /SIZE and no date ends in spaces. */
    while (n > 0 && buf[n - 1] == ' ') {
        buf[--n] = '\0';
    }
    return n;
}

int sh_dir_total(uint32_t files, uint64_t bytes, unsigned cols, char *buf, size_t size)
{
    char sz[16];
    int n;

    if (buf == NULL || size == 0) {
        return -1;
    }
    if (cols & SH_DIR_SIZE) {
        if (sh_fmt_size(bytes, sz, sizeof(sz)) < 0) {
            buf[0] = '\0';
            return -1;
        }
        n = k_snprintf(buf, size, "Total of %u file%s, %s byte%s.", (unsigned)files,
                       files == 1 ? "" : "s", sz, bytes == 1 ? "" : "s");
        /* "12 KiB bytes" reads badly: say "12 KiB." instead. */
        if (bytes >= 10240u) {
            n = k_snprintf(buf, size, "Total of %u file%s, %s.", (unsigned)files,
                           files == 1 ? "" : "s", sz);
        }
    } else {
        n = k_snprintf(buf, size, "Total of %u file%s.", (unsigned)files, files == 1 ? "" : "s");
    }
    n = fit(n, size);
    if (n < 0) {
        buf[0] = '\0';
    }
    return n;
}

int sh_create_end(const char *line)
{
    return line != NULL && line[0] == '.' && line[1] == '\0';
}

/* ---- HELP ---- */

dcl_msg_t sh_help_spec(const dcl_table_t *table, const char *topic, const char **spec)
{
    char word[DCL_TOKEN_MAX + 1];
    size_t i;
    size_t n;
    size_t idx;
    dcl_lookup_t lk;

    *spec = NULL;
    if (table == NULL) {
        return DCL_SYS_NOHELP;
    }
    if (topic == NULL || topic[0] == '\0') {
        topic = "HELP";
    }
    n = k_strlen(topic);
    if (n > DCL_TOKEN_MAX) {
        return DCL_SYS_NOHELP;
    }
    for (i = 0; i < n; i++) {
        word[i] = up(topic[i]);
    }
    word[n] = '\0';
    lk = dcl_lookup(word, table->verbs, table->count, sizeof(table->verbs[0]), &idx);
    if (lk == DCL_LK_AMBIG) {
        return DCL_ABVERB;
    }
    if (lk != DCL_LK_FOUND || table->verbs[idx].help == NULL) {
        return DCL_SYS_NOHELP;
    }
    *spec = table->verbs[idx].help;
    return DCL_NORMAL;
}

/* ---- Programs ---- */

void sh_prog_init(sh_progtab_t *t)
{
    k_memset(t, 0, sizeof(*t));
}

static int prog_name(const char *name, char out[SH_PROG_NAME_MAX + 1])
{
    size_t n;
    size_t i;

    if (name == NULL) {
        return -1;
    }
    n = k_strlen(name);
    if (n == 0 || n > SH_PROG_NAME_MAX) {
        return -1;
    }
    for (i = 0; i < n; i++) {
        char c = up(name[i]);

        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '$' || c == '_')) {
            return -1;
        }
        out[i] = c;
    }
    out[n] = '\0';
    return 0;
}

int sh_prog_add(sh_progtab_t *t, const char *name, sh_prog_start_fn start)
{
    char n[SH_PROG_NAME_MAX + 1];

    if (t == NULL || start == NULL || prog_name(name, n) != 0) {
        return SH_PROG_ERR_ARG;
    }
    if (sh_prog_find(t, n) != NULL) {
        return SH_PROG_ERR_DUP;
    }
    if (t->count >= SH_PROG_MAX) {
        return SH_PROG_ERR_FULL;
    }
    k_memcpy(t->prog[t->count].name, n, sizeof(n));
    t->prog[t->count].start = start;
    t->count++;
    return 0;
}

int sh_prog_put(sh_progtab_t *t, const char *name, sh_prog_start_fn start)
{
    char n[SH_PROG_NAME_MAX + 1];
    unsigned i;

    if (t == NULL || start == NULL || prog_name(name, n) != 0) {
        return SH_PROG_ERR_ARG;
    }
    for (i = 0; i < t->count; i++) {
        if (k_strcmp(t->prog[i].name, n) == 0) {
            t->prog[i].start = start;
            return 0;
        }
    }
    return sh_prog_add(t, n, start);
}

sh_prog_start_fn sh_prog_find(const sh_progtab_t *t, const char *name)
{
    char n[SH_PROG_NAME_MAX + 1];
    unsigned i;

    if (t == NULL || prog_name(name, n) != 0) {
        return NULL;
    }
    for (i = 0; i < t->count; i++) {
        if (k_strcmp(t->prog[i].name, n) == 0) {
            return t->prog[i].start;
        }
    }
    return NULL;
}

/* ---- Markers ---- */

void sh_verb_word(const dcl_cmd_t *cmd, char *out, size_t size)
{
    if (out == NULL || size == 0) {
        return;
    }
    if (cmd == NULL || cmd->verb == NULL) {
        (void)k_snprintf(out, size, "-");
    } else if (cmd->keyword != NULL) {
        (void)k_snprintf(out, size, "%s_%s", cmd->verb->name, cmd->keyword->name);
    } else {
        (void)k_snprintf(out, size, "%s", cmd->verb->name);
    }
}

/* ---- LOGIN ---- */

int sh_login_name(const char *param, char *out, size_t size)
{
    size_t i;

    if (out == NULL || size < UAF_NAME_MAX + 1u) {
        return DCL_SYS_IVUSER;
    }
    out[0] = '\0';
    if (param == NULL || param[0] == '\0') {
        return DCL_NORMAL;
    }
    if (uaf_check_name(param) != UAF_OK) {
        return DCL_SYS_IVUSER;
    }
    for (i = 0; param[i] != '\0'; i++) {
        out[i] = up(param[i]);
    }
    out[i] = '\0';
    return DCL_NORMAL;
}

/* ---- SET HALT and the maintenance request ---- */

int sh_halt_mreq(int qual)
{
    if (qual > 0) {
        return SH_MREQ_WRITE;
    }
    return qual < 0 ? SH_MREQ_CLEAR : SH_MREQ_NONE;
}

int sh_maint_note(const char *user, char *buf, size_t size)
{
    static const char head[] = "UOS ";
    size_t n = 0;
    size_t i;

    if (buf == NULL || size < sizeof(head) + 2u) {
        return -1;
    }
    for (i = 0; head[i] != '\0'; i++) {
        buf[n++] = head[i];
    }
    for (i = 0; user != NULL && user[i] != '\0' && n < 63u && n + 4u <= size; i++) {
        unsigned char c = (unsigned char)user[i];

        buf[n++] = c >= 0x20u && c <= 0x7Eu ? (char)c : '?';
    }
    buf[n++] = '\r';
    buf[n++] = '\n';
    buf[n] = '\0';
    return (int)n;
}
