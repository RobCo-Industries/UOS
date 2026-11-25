/*
 * AUTHORIZE and SET PASSWORD: the commands that change SYSUAF.DAT.
 *
 * Rules:
 *   - A password is read masked with con_read_line(CON_RL_MASK), never
 *     from the command line, never stored in the history, never in a
 *     marker. Every copy is wiped when the command ends.
 *   - Salts come from the hardware RNG. No RNG: the change is refused.
 *   - The new database is built in memory from the session's copy, written
 *     whole with vfs_sys_write_all() (the library's atomic write), then
 *     the session service reads it again with sess_reload().
 *   - One hash per changed password runs in one loop pass: 2000
 *     iterations, the cost of one login check.
 */

#include <robco/klibc.h>
#include <robco/rng.h>

#include "sess.h"
#include "shint.h"
#include "uos.h"

#define UAF_PATH "/UOS/SYSUAF.DAT"

static uaf_db_t work;
static uint8_t image[UAF_MAX_IMAGE];
static char pw_old[UAF_PASSWORD_MAX + 1];
static char pw_new[UAF_PASSWORD_MAX + 1];
static char pw_ver[UAF_PASSWORD_MAX + 1];
static int rng_ok = -1;

static void wipe(void)
{
    k_memset(pw_old, 0, sizeof(pw_old));
    k_memset(pw_new, 0, sizeof(pw_new));
    k_memset(pw_ver, 0, sizeof(pw_ver));
    k_memset(&work, 0, sizeof(work));
    k_memset(image, 0, sizeof(image));
}

static void uaf_stop(sh_job_t *j)
{
    (void)j;
    wipe();
}

static void copy_pw(char *dst, const char *text, unsigned len)
{
    if (len > UAF_PASSWORD_MAX) {
        /* Too long: empty, so the check refuses it. */
        dst[0] = '\0';
        return;
    }
    k_memcpy(dst, text, len);
    dst[len] = '\0';
}

static int salt(uint8_t out[UAF_SALT_SIZE])
{
    if (rng_ok < 0) {
        rng_ok = rng_init() == 0 ? 1 : 0;
    }
    return rng_ok == 1 && rng_read(out, UAF_SALT_SIZE) == 0 ? 0 : -1;
}

/* Writes `work` to the card and reloads it. 0, or 1 after sh_end(). */
static int commit(sh_job_t *j)
{
    size_t len = 0;
    int rc;

    if (uaf_write(&work, image, sizeof(image), &len) != UAF_OK) {
        (void)sh_end(j, DCL_SYS_UAFERR, NULL, 0, 1);
        return 1;
    }
    rc = vfs_sys_write_all(UAF_PATH, image, len);
    k_memset(image, 0, sizeof(image));
    if (rc != VFS_OK) {
        (void)sh_end_err(j, SH_SRC_VFS, rc, NULL);
        return 1;
    }
    if (sess_reload() != 0) {
        (void)sh_end(j, DCL_SYS_UAFERR, NULL, 0, 1);
        return 1;
    }
    UOS_MARK("uaf result=written users=%u", (unsigned)work.count);
    return 0;
}

static int load_work(sh_job_t *j)
{
    const uaf_db_t *db = sess_db();

    if (db == NULL) {
        (void)sh_end(j, DCL_SYS_UAFERR, NULL, 0, 1);
        return 1;
    }
    work = *db;
    return 0;
}

/* ---- Masked reads: three in a row ---- */

typedef void (*pw_next_fn)(sh_job_t *j);

static char *pw_dst;
static pw_next_fn pw_next;

static void on_pw(int result, const char *text, unsigned len, void *ctx)
{
    sh_job_t *j = &sh_job;

    (void)ctx;
    if (!j->active) {
        return;
    }
    if (result != CON_OK) {
        sh_resume(j, sh_end(j, DCL_SYS_CANCEL, NULL, 0, 1));
        return;
    }
    copy_pw(pw_dst, text, len);
    pw_next(j);
}

static int ask_pw(const char *prompt, char *dst, pw_next_fn next)
{
    pw_dst = dst;
    pw_next = next;
    if (sh_ask("password", prompt, CON_RL_MASK, UAF_PASSWORD_MAX + 1u, on_pw) != 0) {
        return sh_end(&sh_job, DCL_SYS_BUSY, NULL, 0, 1);
    }
    return SH_WAIT;
}

/* The new password and its verification: 0 when both agree and pass the
 * rules, else 1 after sh_end(). */
static int check_new(sh_job_t *j)
{
    if (k_strcmp(pw_new, pw_ver) != 0) {
        (void)sh_end(j, DCL_SYS_PWDNOTVER, NULL, 0, 1);
        return 1;
    }
    if (uaf_check_password(pw_new) != UAF_OK) {
        (void)sh_end_err(j, SH_SRC_UAF, UAF_E_PASSWORD_LEN, NULL);
        return 1;
    }
    return 0;
}

/* ---- SET PASSWORD ---- */

static void pwd_ver(sh_job_t *j)
{
    uint8_t s[UAF_SALT_SIZE];
    int rc;

    if (check_new(j) != 0) {
        sh_resume(j, SH_DONE);
        return;
    }
    if (uaf_verify(sess_db(), j->user, pw_old, NULL, NULL) != UAF_OK) {
        sh_resume(j, sh_end_err(j, SH_SRC_UAF, UAF_E_BAD_LOGIN, NULL));
        return;
    }
    if (load_work(j) != 0) {
        sh_resume(j, SH_DONE);
        return;
    }
    if (salt(s) != 0) {
        sh_resume(j, sh_end(j, DCL_SYS_UAFERR, NULL, 0, 1));
        return;
    }
    rc = uaf_set_password(&work, j->user, s, pw_new);
    k_memset(s, 0, sizeof(s));
    if (rc != UAF_OK) {
        sh_resume(j, sh_end_err(j, SH_SRC_UAF, rc, NULL));
        return;
    }
    if (commit(j) != 0) {
        sh_resume(j, SH_DONE);
        return;
    }
    j->tally.done = 1;
    sh_resume(j, sh_end(j, DCL_SYS_PWDSET, NULL, 0, 1));
}

static void pwd_new(sh_job_t *j)
{
    sh_resume(j, ask_pw("Verification:", pw_ver, pwd_ver));
}

static void pwd_old(sh_job_t *j)
{
    sh_resume(j, ask_pw("New password:", pw_new, pwd_new));
}

static int set_password(sh_job_t *j)
{
    if (sess_db() == NULL) {
        return sh_end(j, DCL_SYS_UAFERR, NULL, 0, 1);
    }
    return ask_pw("Old password:", pw_old, pwd_old);
}

/* ---- AUTHORIZE ---- */

static const char *username(const sh_job_t *j)
{
    return j->cmd.nparams > 0 ? j->cmd.params[0] : NULL;
}

static int list_one(sh_job_t *j, const uaf_rec_t *r)
{
    char name[UAF_NAME_MAX + 1];
    char home[UAF_HOME_FIELD + 1];
    char privs[UAF_PRIVS_TEXT_MAX];
    char line[UOS_LINE_MAX + 1];

    k_memcpy(name, r->name, UAF_NAME_MAX);
    name[UAF_NAME_MAX] = '\0';
    k_memcpy(home, r->home, UAF_HOME_FIELD);
    home[UAF_HOME_FIELD] = '\0';
    if (uaf_privs_format(r->privs, privs, sizeof(privs)) != UAF_OK || privs[0] == '\0') {
        k_snprintf(privs, sizeof(privs), "NONE");
    }
    k_snprintf(line, sizeof(line), "%-16s  %-8s  %-28s %s%s", name, home, privs,
               (r->flags & UAF_FLAG_DISABLED) ? "DISABLED " : "",
               (r->flags & UAF_FLAG_NOHACK) ? "NOHACK" : "");
    sh_out(j, line);
    return 0;
}

static int auth_list(sh_job_t *j)
{
    const uaf_db_t *db = sess_db();
    uint32_t i;

    if (db == NULL) {
        return sh_end(j, DCL_SYS_UAFERR, NULL, 0, 1);
    }
    sh_out(j, "Username          Home      Privileges                   Flags");
    if (username(j) != NULL) {
        const uaf_rec_t *r = sess_find(username(j));

        if (r == NULL) {
            return sh_end_err(j, SH_SRC_UAF, UAF_E_NO_USER, username(j));
        }
        (void)list_one(j, r);
        return sh_end(j, DCL_NORMAL, NULL, 0, 0);
    }
    for (i = 0; i < db->count; i++) {
        (void)list_one(j, &db->rec[i]);
    }
    j->tally.done = db->count;
    return sh_end(j, DCL_NORMAL, NULL, 0, 0);
}

/* The privilege mask of /PRIVS, /NOPRIVS, or `keep` when neither is given. */
static int quals_privs(const sh_job_t *j, uint32_t keep, uint32_t *out)
{
    const dcl_qval_t *q = dcl_cmd_qual(&j->cmd, "PRIVS");
    unsigned i;

    *out = keep;
    if (q == NULL) {
        return 0;
    }
    *out = 0;
    if (q->negated) {
        return 0;
    }
    for (i = 0; i < q->nvalues; i++) {
        uint32_t bit;

        if (uaf_privs_parse(q->values[i], &bit) != UAF_OK) {
            return -1;
        }
        *out |= bit;
    }
    return 0;
}

static uint32_t quals_flags(const sh_job_t *j, uint32_t flags)
{
    const dcl_qval_t *d = dcl_cmd_qual(&j->cmd, "DISABLED");
    const dcl_qval_t *h = dcl_cmd_qual(&j->cmd, "HACKABLE");

    if (d != NULL) {
        flags = d->negated ? (flags & ~UAF_FLAG_DISABLED) : (flags | UAF_FLAG_DISABLED);
    }
    if (h != NULL) {
        flags = h->negated ? (flags | UAF_FLAG_NOHACK) : (flags & ~UAF_FLAG_NOHACK);
    }
    return flags;
}

/* The add or modify after the password reads, if any. */
static void auth_apply(sh_job_t *j)
{
    const char *name = username(j);
    const char *home = dcl_cmd_value(&j->cmd, "HOME");
    int add = k_strcmp(j->cmd.keyword->name, "ADD") == 0;
    int pw = add || dcl_cmd_qual(&j->cmd, "PASSWORD") != NULL;
    uint8_t s[UAF_SALT_SIZE];
    int idx;
    uint32_t privs;
    int rc;

    if (pw && check_new(j) != 0) {
        sh_resume(j, SH_DONE);
        return;
    }
    if (load_work(j) != 0) {
        sh_resume(j, SH_DONE);
        return;
    }
    if (pw && salt(s) != 0) {
        sh_resume(j, sh_end(j, DCL_SYS_UAFERR, NULL, 0, 1));
        return;
    }
    idx = uaf_find(&work, name);
    if (add) {
        (void)quals_privs(j, 0, &privs);
        rc = uaf_add(&work, name, home != NULL ? home : name, privs, quals_flags(j, 0), s, pw_new);
    } else {
        rc = idx < 0 ? UAF_E_NO_USER : UAF_OK;
        if (rc == UAF_OK && quals_privs(j, work.rec[idx].privs, &privs) == 0) {
            rc = uaf_set_privs(&work, name, privs);
        }
        if (rc == UAF_OK) {
            rc = uaf_set_flags(&work, name, quals_flags(j, work.rec[idx].flags));
        }
        if (rc == UAF_OK && home != NULL) {
            rc = uaf_set_home(&work, name, home);
        }
        if (rc == UAF_OK && pw) {
            rc = uaf_set_password(&work, name, s, pw_new);
        }
    }
    k_memset(s, 0, sizeof(s));
    if (rc != UAF_OK) {
        sh_resume(j, sh_end_err(j, SH_SRC_UAF, rc, name));
        return;
    }
    if (commit(j) != 0) {
        sh_resume(j, SH_DONE);
        return;
    }
    j->tally.done = 1;
    if (add) {
        char spec[32];
        const uaf_rec_t *r = &work.rec[work.count - 1u];
        char h[UAF_HOME_FIELD + 1];

        /* The home folder, made with the caller's own rights. A refusal
         * leaves the user added: an operator makes the folder. */
        k_memcpy(h, r->home, UAF_HOME_FIELD);
        h[UAF_HOME_FIELD] = '\0';
        k_snprintf(spec, sizeof(spec), "SD0:[HOME.%s]", h);
        rc = vfs_mkdir(j->ctx, j->privs, spec);
        if (rc != VFS_OK && rc != VFS_E_EXISTS) {
            sh_say(j, sh_err_msg(SH_SRC_VFS, rc, -1, 0), spec, 0);
        }
    }
    sh_resume(j, sh_end(j, add ? DCL_SYS_UADDED : DCL_SYS_UMODIFIED, name, 0, 1));
}

static void auth_new(sh_job_t *j)
{
    sh_resume(j, ask_pw("Verification:", pw_ver, auth_apply));
}

static int auth_change(sh_job_t *j)
{
    const char *name = username(j);
    int add = k_strcmp(j->cmd.keyword->name, "ADD") == 0;
    uint32_t privs;

    if (sess_db() == NULL) {
        return sh_end(j, DCL_SYS_UAFERR, NULL, 0, 1);
    }
    if (uaf_check_name(name) != UAF_OK) {
        return sh_end_err(j, SH_SRC_UAF, UAF_E_NAME, name);
    }
    if (add && sess_find(name) != NULL) {
        return sh_end_err(j, SH_SRC_UAF, UAF_E_EXISTS, name);
    }
    if (!add && sess_find(name) == NULL) {
        return sh_end_err(j, SH_SRC_UAF, UAF_E_NO_USER, name);
    }
    if (quals_privs(j, 0, &privs) != 0) {
        return sh_end_err(j, SH_SRC_UAF, UAF_E_PRIV, NULL);
    }
    if (dcl_cmd_value(&j->cmd, "HOME") != NULL &&
        uaf_check_home(dcl_cmd_value(&j->cmd, "HOME")) != UAF_OK) {
        return sh_end_err(j, SH_SRC_UAF, UAF_E_HOME, dcl_cmd_value(&j->cmd, "HOME"));
    }
    if (add && dcl_cmd_value(&j->cmd, "HOME") == NULL && uaf_check_home(name) != UAF_OK) {
        /* The user name is not a folder name: /HOME must say one. */
        return sh_end_err(j, SH_SRC_UAF, UAF_E_HOME, name);
    }
    if (add || dcl_cmd_qual(&j->cmd, "PASSWORD") != NULL) {
        return ask_pw("Password:", pw_new, auth_new);
    }
    auth_apply(j);
    return SH_WAIT;
}

static int auth_remove(sh_job_t *j)
{
    const char *name = username(j);
    int rc;

    if (sess_db() == NULL) {
        return sh_end(j, DCL_SYS_UAFERR, NULL, 0, 1);
    }
    if (uaf_find(sess_db(), name) == uaf_find(sess_db(), j->user)) {
        return sh_end(j, DCL_SYS_SELF, name, 0, 1);
    }
    if (load_work(j) != 0) {
        return SH_DONE;
    }
    rc = uaf_remove(&work, name);
    if (rc != UAF_OK) {
        return sh_end_err(j, SH_SRC_UAF, rc, name);
    }
    if (commit(j) != 0) {
        return SH_DONE;
    }
    j->tally.done = 1;
    return sh_end(j, DCL_SYS_UREMOVED, name, 0, 1);
}

const sh_impl_t sh_uaf_cmds[] = {
    { "SET", "PASSWORD", set_password, NULL, uaf_stop },
    { "AUTHORIZE", "LIST", auth_list, NULL, NULL },
    { "AUTHORIZE", "ADD", auth_change, NULL, uaf_stop },
    { "AUTHORIZE", "MODIFY", auth_change, NULL, uaf_stop },
    { "AUTHORIZE", "REMOVE", auth_remove, NULL, uaf_stop },
};

const unsigned sh_uaf_cmds_count = sizeof(sh_uaf_cmds) / sizeof(sh_uaf_cmds[0]);
