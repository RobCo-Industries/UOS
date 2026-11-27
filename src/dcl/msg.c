/* The DCL message catalogue and its formatter. Pure. */

#include "dcl.h"

#include <robco/klibc.h>

typedef struct {
    const char *facility;
    char severity;
    const char *ident;
    const char *text;
} dcl_msgdef_t;

static const dcl_msgdef_t msgs[DCL_MSG_COUNT] = {
    [DCL_NORMAL] = { "DCL", 'S', "NORMAL", "normal successful completion" },
    [DCL_CONTIN] = { "DCL", 'I', "CONTIN", "command continues on the next line" },
    [DCL_IVVERB] = { "DCL", 'W', "IVVERB", "unrecognized command verb - check validity and spelling" },
    [DCL_ABVERB] = { "DCL", 'W', "ABVERB", "ambiguous command verb - supply more characters" },
    [DCL_IVKEYW] = { "DCL", 'W', "IVKEYW", "unrecognized keyword - check validity and spelling" },
    [DCL_ABKEYW] = { "DCL", 'W', "ABKEYW", "ambiguous keyword - supply more characters" },
    [DCL_IVQUAL] = { "DCL", 'W', "IVQUAL", "unrecognized qualifier" },
    [DCL_ABQUAL] = { "DCL", 'W', "ABQUAL", "ambiguous qualifier - supply more characters" },
    [DCL_NONEG] = { "DCL", 'W', "NONEG", "qualifier cannot be negated" },
    [DCL_INSFPRM] = { "DCL", 'W', "INSFPRM", "missing command parameters - supply all required parameters" },
    [DCL_MAXPARM] = { "DCL", 'W', "MAXPARM", "too many parameters" },
    [DCL_MAXQUAL] = { "DCL", 'W', "MAXQUAL", "too many qualifiers" },
    [DCL_VALREQ] = { "DCL", 'W', "VALREQ", "missing qualifier or keyword value" },
    [DCL_NOVALU] = { "DCL", 'W', "NOVALU", "value not allowed" },
    [DCL_NOLIST] = { "DCL", 'W', "NOLIST", "list of values not allowed" },
    [DCL_MAXVAL] = { "DCL", 'W', "MAXVAL", "too many values in list" },
    [DCL_IVNUMB] = { "DCL", 'W', "IVNUMB", "invalid number" },
    [DCL_NUMRNG] = { "DCL", 'W', "NUMRNG", "number out of range" },
    [DCL_UNTERM] = { "DCL", 'W', "UNTERM", "unterminated quoted string" },
    [DCL_PARENS] = { "DCL", 'W', "PARENS", "missing closing parenthesis" },
    [DCL_IVCHAR] = { "DCL", 'W', "IVCHAR", "invalid character in command" },
    [DCL_LINOVF] = { "DCL", 'W', "LINOVF", "command line too long" },
    [DCL_TKNOVF] = { "DCL", 'W', "TKNOVF", "command element too long" },
    [DCL_SYS_NOPRIV] = { "UOS", 'E', "NOPRIV", "not enough privilege for this operation" },
    [DCL_SYS_FNF] = { "UOS", 'E', "FNF", "file not found" },
    [DCL_SYS_DNF] = { "UOS", 'E', "DNF", "directory not found" },
    [DCL_SYS_NOFILES] = { "UOS", 'W', "NOFILES", "no files found" },
    [DCL_SYS_EXISTS] = { "UOS", 'E', "EXISTS", "file already exists" },
    [DCL_SYS_IVSPEC] = { "UOS", 'E', "IVSPEC", "invalid file specification" },
    [DCL_SYS_READERR] = { "UOS", 'E', "READERR", "error reading file" },
    [DCL_SYS_WRITERR] = { "UOS", 'E', "WRITERR", "error writing file" },
    [DCL_SYS_DISKFULL] = { "UOS", 'E', "DISKFULL", "no space left on the volume" },
    [DCL_SYS_NODEV] = { "UOS", 'E', "NODEV", "no such device" },
    [DCL_SYS_NOUSER] = { "UOS", 'E', "NOUSER", "no such user" },
    [DCL_SYS_CANCEL] = { "UOS", 'I', "CANCEL", "operation cancelled" },
    [DCL_SYS_COPIED] = { "COPY", 'S', "COPIED", "!N file!P copied" },
    [DCL_SYS_DELETED] = { "DELETE", 'S', "DELETED", "!N file!P deleted" },
    [DCL_SYS_RENAMED] = { "RENAME", 'S', "RENAMED", "!N file!P renamed" },
    [DCL_SYS_CREATED] = { "CREATE", 'S', "CREATED", "!S created" },
    [DCL_SYS_WRITLCK] = { "UOS", 'E', "WRITLCK", "volume is read-only" },
    [DCL_SYS_PROTECT] = { "UOS", 'E', "PROTECT", "file is protected" },
    [DCL_SYS_TOOBIG] = { "UOS", 'E', "TOOBIG", "file is too large" },
    [DCL_SYS_INSFMEM] = { "UOS", 'E', "INSFMEM", "not enough memory for this operation" },
    [DCL_SYS_NOTINST] = { "UOS", 'W', "NOTINST", "!S is not installed in this build" },
    [DCL_SYS_NOHELP] = { "HELP", 'W', "NOHELP", "no help is available on that topic" },
    [DCL_SYS_SENSOR] = { "DEVICE", 'E', "SENSOR", "the device is a sensor and takes no command" },
    [DCL_SYS_IVACT] = { "DEVICE", 'E', "IVACT", "action not valid for this device" },
    [DCL_SYS_DEVERR] = { "DEVICE", 'E', "DEVERR", "the device command failed" },
    [DCL_SYS_NOMAINT] = { "UOS", 'E', "NOMAINT", "maintenance request not written: !S; the terminal stays up" },
    [DCL_SYS_MREQKEPT] = { "UOS", 'E', "MREQKEPT", "maintenance request not removed: !S; the terminal stays up" },
    [DCL_SYS_BUSY] = { "UOS", 'W', "BUSY", "a command is already running" },
    [DCL_SYS_NOSESS] = { "UOS", 'E', "NOSESS", "nobody is logged in" },
    [DCL_SYS_NOCLOCK] = { "UOS", 'W', "NOCLOCK", "no clock chip answered: the date is not known" },
    [DCL_SYS_PWDSET] = { "SET", 'S', "PWDSET", "password changed" },
    [DCL_SYS_PWDNOTVER] = { "SET", 'E', "PWDNOTVER", "new password not verified: password not changed" },
    [DCL_SYS_BADPWD] = { "SET", 'E', "BADPWD", "password is not correct" },
    [DCL_SYS_IVPWD] = { "UAF", 'E', "IVPWD", "a password is 1 to 32 printable characters" },
    [DCL_SYS_UADDED] = { "UAF", 'S', "ADDED", "user !S added" },
    [DCL_SYS_UMODIFIED] = { "UAF", 'S', "MODIFIED", "user !S modified" },
    [DCL_SYS_UREMOVED] = { "UAF", 'S', "REMOVED", "user !S removed" },
    [DCL_SYS_UEXISTS] = { "UAF", 'E', "EXISTS", "user already exists" },
    [DCL_SYS_UAFFULL] = { "UAF", 'E', "FULL", "the user database is full" },
    [DCL_SYS_IVUSER] = { "UAF", 'E', "IVNAME", "invalid user name" },
    [DCL_SYS_IVHOME] = { "UAF", 'E', "IVHOME", "invalid home folder name" },
    [DCL_SYS_SELF] = { "UAF", 'E', "SELF", "the user who is logged in cannot be removed" },
    [DCL_SYS_UAFERR] = { "UAF", 'E', "UAFERR", "the user database cannot be changed" },
    [DCL_SYS_FILCOPY] = { "COPY", 'I', "FILCOPY", "!S copied" },
    [DCL_SYS_FILDEL] = { "DELETE", 'I', "FILDEL", "!S deleted" },
    [DCL_SYS_FILREN] = { "RENAME", 'I', "FILREN", "!S renamed" },
    [DCL_SYS_PROGERR] = { "UOS", 'E', "PROGERR", "the program could not start" },
    [DCL_SYS_SKIPPED] = { "UOS", 'I', "SKIPPED", "!N file!P skipped: not enough privilege" },
    [DCL_SYS_CFGERR] = { "DEVICE", 'W', "CFGERR", "DEVICES.CFG has !N errors: the serial line names each" },
};

static const dcl_msgdef_t *def_of(dcl_msg_t id)
{
    if ((unsigned)id >= DCL_MSG_COUNT) {
        return NULL;
    }
    return &msgs[id];
}

const char *dcl_msg_facility(dcl_msg_t id)
{
    const dcl_msgdef_t *d = def_of(id);

    return d != NULL ? d->facility : NULL;
}

char dcl_msg_severity(dcl_msg_t id)
{
    const dcl_msgdef_t *d = def_of(id);

    return d != NULL ? d->severity : '\0';
}

const char *dcl_msg_ident(dcl_msg_t id)
{
    const dcl_msgdef_t *d = def_of(id);

    return d != NULL ? d->ident : NULL;
}

const char *dcl_msg_text(dcl_msg_t id)
{
    const dcl_msgdef_t *d = def_of(id);

    return d != NULL ? d->text : NULL;
}

typedef struct {
    char *buf;
    size_t size;
    size_t len;
    int over;
} out_t;

static void put(out_t *o, const char *s, size_t n)
{
    if (o->over || o->len + n + 1 > o->size) {
        o->over = 1;
        return;
    }
    k_memcpy(o->buf + o->len, s, n);
    o->len += n;
}

static void puts_(out_t *o, const char *s)
{
    put(o, s, k_strlen(s));
}

static void put_num(out_t *o, uint32_t v)
{
    char tmp[11];
    size_t i = sizeof(tmp);

    do {
        tmp[--i] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v != 0);
    put(o, tmp + i, sizeof(tmp) - i);
}

int dcl_format_message(char *buf, size_t size, dcl_msg_t id, const char *arg, uint32_t num)
{
    const dcl_msgdef_t *d = def_of(id);
    out_t o = { buf, size, 0, 0 };
    const char *t;
    int used_arg = 0;
    char sev[2];

    if (buf == NULL || size == 0) {
        return -1;
    }
    buf[0] = '\0';
    if (d == NULL) {
        return -1;
    }
    sev[0] = d->severity;
    sev[1] = '\0';
    puts_(&o, "%");
    puts_(&o, d->facility);
    puts_(&o, "-");
    puts_(&o, sev);
    puts_(&o, "-");
    puts_(&o, d->ident);
    puts_(&o, ", ");
    for (t = d->text; *t != '\0'; t++) {
        if (t[0] == '!' && (t[1] == 'S' || t[1] == 'N' || t[1] == 'P')) {
            if (t[1] == 'S') {
                puts_(&o, arg != NULL ? arg : "");
                used_arg = 1;
            } else if (t[1] == 'N') {
                put_num(&o, num);
            } else if (num != 1) {
                puts_(&o, "s");
            }
            t++;
            continue;
        }
        put(&o, t, 1);
    }
    if (!used_arg && arg != NULL) {
        puts_(&o, "\n \\");
        puts_(&o, arg);
        puts_(&o, "\\");
    }
    if (o.over) {
        buf[0] = '\0';
        return -1;
    }
    buf[o.len] = '\0';
    return (int)o.len;
}
