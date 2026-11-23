/*
 * The file system service's decisions (pure). See vfsrules.h.
 */
#include "vfsrules.h"

#include <robco/klibc.h>

static const char *const vfs_words[VFS_E_COUNT] = {
    "ok", "nocard", "notree", "badspec", "notfound", "dirnotfound", "isdir", "exists",
    "nopriv", "readonly", "protected", "toobig", "readerr", "writeerr", "full", "nomem",
    "nodev", "nofiles", "busy", "badarg",
};

const char *vfs_strerror(int err)
{
    return (err <= 0 && err > -VFS_E_COUNT) ? vfs_words[-err] : "unknown";
}

dcl_msg_t vfs_msg(int err)
{
    switch (err) {
    case VFS_OK:          return DCL_NORMAL;
    case VFS_E_NOCARD:
    case VFS_E_NOTREE:
    case VFS_E_NODEV:     return DCL_SYS_NODEV;
    case VFS_E_SPEC:
    case VFS_E_ISDIR:
    case VFS_E_ARG:       return DCL_SYS_IVSPEC;
    case VFS_E_NOTFOUND:  return DCL_SYS_FNF;
    case VFS_E_DIRNF:     return DCL_SYS_DNF;
    case VFS_E_EXISTS:    return DCL_SYS_EXISTS;
    case VFS_E_NOPRIV:    return DCL_SYS_NOPRIV;
    case VFS_E_READONLY:  return DCL_SYS_WRITLCK;
    case VFS_E_PROTECTED: return DCL_SYS_PROTECT;
    case VFS_E_TOOBIG:    return DCL_SYS_TOOBIG;
    case VFS_E_NOMEM:     return DCL_SYS_INSFMEM;
    case VFS_E_READ:      return DCL_SYS_READERR;
    case VFS_E_WRITE:
    case VFS_E_BUSY:      return DCL_SYS_WRITERR;
    case VFS_E_FULL:      return DCL_SYS_DISKFULL;
    case VFS_E_NOFILES:   return DCL_SYS_NOFILES;
    default:              return DCL_SYS_READERR;
    }
}

int vfs_from_fs(int fs_err, int writing)
{
    int io = writing ? VFS_E_WRITE : VFS_E_READ;

    if (fs_err >= 0) {
        return VFS_OK;
    }
    switch (fs_err) {
    case FS_ERR_NOENT:       return VFS_E_NOTFOUND;
    case FS_ERR_EXIST:       return VFS_E_EXISTS;
    case FS_ERR_ISDIR:       return VFS_E_ISDIR;
    case FS_ERR_NAME:
    case FS_ERR_NAMETOOLONG:
    case FS_ERR_INVAL:       return VFS_E_SPEC;
    case FS_ERR_NOSPC:       return VFS_E_FULL;
    case FS_ERR_RO:          return VFS_E_READONLY;
    case FS_ERR_DENIED:      return VFS_E_WRITE;
    case FS_ERR_NOVOL:
    case FS_ERR_NOFS:
    case FS_ERR_NOTREADY:    return VFS_E_NOCARD;
    case FS_ERR_NOMEM:       return VFS_E_NOMEM;
    case FS_ERR_TOOBIG:      return VFS_E_TOOBIG;
    default:                 return io;
    }
}

int vfs_from_vp(int vp_err)
{
    switch (vp_err) {
    case VP_OK:          return VFS_OK;
    case VP_E_NOPRIV:    return VFS_E_NOPRIV;
    case VP_E_READONLY:  return VFS_E_READONLY;
    case VP_E_PROTECTED: return VFS_E_PROTECTED;
    case VP_E_NODEV:     return VFS_E_NODEV;
    default:             return VFS_E_SPEC;
    }
}

static const char vfs_months[12][4] = {
    "JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC",
};

int vfs_fmt_datetime(uint16_t date, uint16_t time, char *buf, size_t size)
{
    unsigned year = 1980u + (date >> 9);
    unsigned month = (date >> 5) & 0x0fu;
    unsigned day = date & 0x1fu;
    unsigned hour = time >> 11;
    unsigned min = (time >> 5) & 0x3fu;
    unsigned sec = (time & 0x1fu) * 2u;

    if (buf == NULL || size < VFS_DATE_LEN + 1) {
        if (buf != NULL && size > 0) {
            buf[0] = '\0';
        }
        return VFS_E_ARG;
    }
    if (month < 1 || month > 12 || day < 1 || hour > 23 || min > 59 || sec > 59) {
        k_memcpy(buf, "?\?-?\?\?-?\?\?\? ?\?:?\?:?\?", VFS_DATE_LEN + 1);
        return VFS_OK;
    }
    (void)k_snprintf(buf, size, "%02u-%s-%04u %02u:%02u:%02u", day, vfs_months[month - 1],
                     year, hour, min, sec);
    return VFS_OK;
}

int vfs_work_name(const char *path, char mark, char *out, size_t size)
{
    size_t n, cut, i;

    if (out != NULL && size > 0) {
        out[0] = '\0';
    }
    if (path == NULL || out == NULL || (mark != '~' && mark != '#')) {
        return VFS_E_ARG;
    }
    n = k_strlen(path);
    cut = n;
    while (cut > 0 && path[cut - 1] != '/') {
        cut--;
    }
    if (cut == n || n + 2 > size) {
        return VFS_E_ARG;
    }
    for (i = 0; i < cut; i++) {
        out[i] = path[i];
    }
    out[cut] = mark;
    for (i = cut; i <= n; i++) {
        out[i + 1] = path[i];
    }
    return VFS_OK;
}

int vfs_atomic_pick(int target, int work_new, int work_old)
{
    if (target || !work_old) {
        return 0;
    }
    return work_new ? '~' : '#';
}

static int vfs_has_wild(const char *s)
{
    for (; *s != '\0'; s++) {
        if (*s == '*' || *s == '%') {
            return 1;
        }
    }
    return 0;
}

static int vfs_take(const char *src, const char *pat, char *out, size_t max)
{
    const char *from = pat;

    if (k_strcmp(pat, "*") == 0) {
        from = src;
    } else if (vfs_has_wild(pat)) {
        return VFS_E_SPEC;
    }
    if (k_strlen(from) > max) {
        return VFS_E_SPEC;
    }
    k_strncpy(out, from, max + 1);
    out[max] = '\0';
    return VFS_OK;
}

int vfs_wild_target(const char *sname, const char *stype, const char *tname,
                    const char *ttype, int tdir, char *oname, char *otype)
{
    int rc;

    if (tdir && tname[0] == '\0') {
        tname = "*";
        ttype = "*";
    }
    rc = vfs_take(sname, tname, oname, VP_NAME_MAX);
    if (rc == VFS_OK) {
        rc = vfs_take(stype, ttype, otype, VP_TYPE_MAX);
    }
    if (rc == VFS_OK && oname[0] == '\0') {
        rc = VFS_E_SPEC;
    }
    return rc;
}

int vfs_parent_res(const vp_res_t *r, vp_res_t *dir)
{
    size_t n;

    *dir = *r;
    if (r->is_dir) {
        return VFS_OK;
    }
    n = k_strlen(dir->path);
    while (n > 0 && dir->path[n - 1] != '/') {
        n--;
    }
    if (n < 2) {
        return VFS_E_SPEC;
    }
    dir->path[n - 1] = '\0';
    dir->name[0] = '\0';
    dir->type[0] = '\0';
    dir->is_dir = 1;
    dir->wild = 0;
    return VFS_OK;
}

int vfs_entry_res(const vp_res_t *dir, const char *fatname, int is_dir, vp_res_t *out)
{
    vp_entry_t e;
    size_t n;

    if (vp_entry_from_fat(fatname, is_dir, &e) != VP_OK) {
        return VFS_E_SPEC;
    }
    k_memset(out, 0, sizeof(*out));
    k_memcpy(out->dir, dir->dir, sizeof(out->dir));
    out->tree = dir->tree;
    out->depth = dir->depth;
    if (is_dir) {
        if (dir->depth >= VP_DEPTH_MAX ||
            (e.type[0] != '\0' && k_strcmp(e.type, "DIR") != 0)) {
            return VFS_E_SPEC;
        }
        k_memcpy(out->dir[out->depth], e.name, sizeof(e.name));
        out->depth++;
        out->is_dir = 1;
    } else {
        k_memcpy(out->name, e.name, sizeof(e.name));
        k_memcpy(out->type, e.type, sizeof(e.type));
    }
    /* The FAT path: the folder, "/", then NAME[.TYPE]. */
    n = k_strlen(dir->path);
    if (k_snprintf(out->path, sizeof(out->path), "%s/%s%s%s", dir->path, e.name,
                   (is_dir || e.type[0] == '\0') ? "" : ".", is_dir ? "" : e.type) >=
        (int)sizeof(out->path) || n == 0) {
        return VFS_E_SPEC;
    }
    return VFS_OK;
}

int vfs_list_keep(const vp_res_t *dir, const char *fatname, int is_dir, int leftover,
                  const char *pname, const char *ptype, const char *home, uint32_t privs,
                  int op, vp_res_t *out)
{
    const char *type;

    if (leftover || fatname == NULL || fatname[0] == '~' || fatname[0] == '#') {
        return 0;
    }
    if (vfs_entry_res(dir, fatname, is_dir, out) != VFS_OK) {
        return 0;
    }
    type = is_dir ? "DIR" : out->type;
    if (!vp_match(pname, ptype, is_dir ? out->dir[out->depth - 1] : out->name, type)) {
        return 0;
    }
    if (vp_access(out, home, privs, VP_OP_LIST) != VP_OK) {
        return 0;
    }
    if (op != VP_OP_LIST && vp_access(out, home, privs, op) != VP_OK) {
        return 0;
    }
    return 1;
}

/* The path module's order. */
static int vfs_cmp(const vfs_entry_t *a, const vfs_entry_t *b)
{
    vp_entry_t x, y;

    k_memset(&x, 0, sizeof(x));
    k_memset(&y, 0, sizeof(y));
    k_memcpy(x.name, a->name, sizeof(x.name));
    k_memcpy(x.type, a->type, sizeof(x.type));
    k_memcpy(y.name, b->name, sizeof(y.name));
    k_memcpy(y.type, b->type, sizeof(y.type));
    return vp_entry_cmp(&x, &y);
}

void vfs_list_insert(vfs_entry_t *e, size_t *count, size_t max, const vfs_entry_t *n)
{
    size_t j = *count;

    if (max == 0) {
        return;
    }
    if (j == max) {
        if (vfs_cmp(&e[max - 1], n) <= 0) {
            return;
        }
        j = max - 1;
    } else {
        (*count)++;
    }
    while (j > 0 && vfs_cmp(&e[j - 1], n) > 0) {
        e[j] = e[j - 1];
        j--;
    }
    e[j] = *n;
}
