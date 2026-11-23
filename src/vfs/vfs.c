/*
 * The file system service's calls over the library's FatFs glue. Thin:
 * the decisions live in path.c and vfsrules.c. See vfs.h.
 */
#include "vfs.h"

#include <robco/alloc.h>
#include <robco/fs.h>
#include <robco/irq.h>
#include <robco/klibc.h>

#include "uos.h"
#include "vfsrules.h"

/* The volume main.c mounts. */
#define VFS_VOLUME   "sd"
/* A DEV: status line, at most. */
#define VFS_DEV_LINE 128u

static const vfs_dev_hooks_t *dev_hooks;
static int last_vp;
/* Large scratch kept off the stack. One caller at a time: the loop. */
static fs_stat_t st_a, st_b;
static fs_dir_t dir_h;

/* ---- Small helpers ---------------------------------------------------------- */

static void *vfs_alloc(size_t n)
{
    uint32_t s = irq_save();
    void *p = k_malloc(n);

    irq_restore(s);
    return p;
}

void vfs_free(void *buf)
{
    uint32_t s;

    if (buf != NULL) {
        s = irq_save();
        k_free(buf);
        irq_restore(s);
    }
}

int vfs_ready(void)
{
    if (uos.card == UOS_CARD_NONE) {
        return VFS_E_NOCARD;
    }
    return uos.card == UOS_CARD_OK ? VFS_OK : VFS_E_NOTREE;
}

int vfs_last_vp(void)
{
    return last_vp;
}

static int vp_rc(int rc)
{
    last_vp = rc;
    return vfs_from_vp(rc);
}

static int resolve(const vp_ctx_t *ctx, const char *spec, const char *deftype, unsigned flags,
                   vp_res_t *r)
{
    int rc;

    if (ctx == NULL || spec == NULL || r == NULL) {
        return VFS_E_ARG;
    }
    rc = vfs_ready();
    if (rc != VFS_OK) {
        return rc;
    }
    last_vp = VP_OK;
    rc = vp_resolve(ctx, spec, deftype, flags, r);
    return rc == VP_OK ? VFS_OK : vp_rc(rc);
}

static int check_access(const vp_ctx_t *ctx, uint32_t privs, const vp_res_t *r, int op)
{
    int rc = vp_access(r, ctx->home, privs, op);

    return rc == VP_OK ? VFS_OK : vp_rc(rc);
}

/* "NAME.DIR" names the folder NAME. */
static int norm_dir(vp_res_t *r)
{
    vp_res_t parent;
    char fat[VP_NAME_MAX + 1];

    if (r->is_dir || r->wild || r->tree == VP_TREE_DEV || k_strcmp(r->type, "DIR") != 0) {
        return VFS_OK;
    }
    if (vfs_parent_res(r, &parent) != VFS_OK) {
        return VFS_E_SPEC;
    }
    k_memcpy(fat, r->name, sizeof(fat));
    return vfs_entry_res(&parent, fat, 1, r);
}

/* Why a path is missing: its folder is missing too, or only the file. */
static int missing(const vp_res_t *r)
{
    vp_res_t parent;

    if (r->is_dir) {
        return VFS_E_DIRNF;
    }
    if (vfs_parent_res(r, &parent) != VFS_OK) {
        return VFS_E_NOTFOUND;
    }
    if (parent.depth == 0) {
        return VFS_E_NOTFOUND;   /* the tree root is always there */
    }
    if (fs_stat(parent.path, &st_b) != FS_OK || !st_b.is_dir) {
        return VFS_E_DIRNF;
    }
    return VFS_E_NOTFOUND;
}

/* fs_stat() of a resolved path: VFS_OK with st_a filled, or the reason it is not there. */
static int stat_res(const vp_res_t *r)
{
    int rc = fs_stat(r->path, &st_a);

    if (rc == FS_ERR_NOENT) {
        return missing(r);
    }
    return vfs_from_fs(rc, 0);
}

/* The folder a result names, or holds. */
static int folder_of(const vp_res_t *r, vp_res_t *dir)
{
    if (r->wild) {
        *dir = *r;
        dir->name[0] = '\0';
        dir->type[0] = '\0';
        dir->is_dir = 1;
        dir->wild = 0;
        return VFS_OK;
    }
    return vfs_parent_res(r, dir);
}

/* The target of a copy or rename: `to` against the source's name. */
static int target_res(const vp_ctx_t *ctx, const vp_res_t *src, const char *to, vp_res_t *t)
{
    vp_res_t r, dir;
    char name[VP_NAME_MAX + 1], type[VP_TYPE_MAX + 1];
    char fat[VP_NAME_MAX + VP_TYPE_MAX + 2];
    int rc = resolve(ctx, to, NULL, VP_R_WILD_OK, &r);

    if (rc != VFS_OK) {
        return rc;
    }
    if (r.tree == VP_TREE_DEV) {
        return VFS_E_NOPRIV;
    }
    rc = vfs_wild_target(src->name, src->type, r.name, r.type, r.is_dir, name, type);
    if (rc == VFS_OK) {
        rc = r.is_dir ? (dir = r, VFS_OK) : folder_of(&r, &dir);
    }
    if (rc != VFS_OK) {
        return rc;
    }
    (void)k_snprintf(fat, sizeof(fat), "%s%s%s", name, type[0] != '\0' ? "." : "", type);
    return vfs_entry_res(&dir, fat, 0, t);
}

/* ---- DEV: -------------------------------------------------------------------- */

void vfs_dev_register(const vfs_dev_hooks_t *hooks)
{
    dev_hooks = hooks;
}

static int dev_read(const vp_res_t *r, size_t max_bytes, char **buf, size_t *len)
{
    char *p;
    int rc;

    if (dev_hooks == NULL || dev_hooks->read == NULL || r->is_dir) {
        return VFS_E_NODEV;
    }
    p = vfs_alloc(VFS_DEV_LINE);
    if (p == NULL) {
        return VFS_E_NOMEM;
    }
    rc = dev_hooks->read(r->path, p, VFS_DEV_LINE);
    if (rc >= 0 && (size_t)rc >= VFS_DEV_LINE) {
        rc = VFS_E_READ;
    }
    if (rc >= 0 && (size_t)rc > max_bytes) {
        rc = VFS_E_TOOBIG;
    }
    if (rc < 0) {
        vfs_free(p);
        return rc;
    }
    p[rc] = '\0';
    *buf = p;
    *len = (size_t)rc;
    return VFS_OK;
}

static int dev_write(const vp_res_t *r, const void *data, size_t len, uint32_t privs)
{
    char text[VFS_DEV_LINE];

    if (dev_hooks == NULL || dev_hooks->write == NULL || r->is_dir) {
        return VFS_E_NODEV;
    }
    if (len >= sizeof(text)) {
        return VFS_E_TOOBIG;
    }
    k_memcpy(text, data, len);
    text[len] = '\0';
    return dev_hooks->write(r->path, text, privs);
}

/* ---- Whole files --------------------------------------------------------------- */

/* Reads `path`, already checked, into a heap buffer. */
static int read_path(const vp_res_t *r, size_t max_bytes, char **buf, size_t *len)
{
    size_t got = 0;
    char *p;
    int rc;

    if (r->tree == VP_TREE_UOS) {
        /* Finishes or undoes an interrupted atomic write of this file. */
        (void)fs_atomic_recover(r->path);
    }
    rc = stat_res(r);
    if (rc != VFS_OK) {
        return rc;
    }
    if (st_a.is_dir) {
        return VFS_E_ISDIR;
    }
    if (st_a.size > max_bytes) {
        return VFS_E_TOOBIG;
    }
    p = vfs_alloc((size_t)st_a.size + 1u);
    if (p == NULL) {
        return VFS_E_NOMEM;
    }
    rc = st_a.size == 0 ? FS_OK : fs_read_file(r->path, p, st_a.size, &got);
    if (rc != FS_OK) {
        vfs_free(p);
        return vfs_from_fs(rc, 0);
    }
    p[got] = '\0';
    *buf = p;
    *len = got;
    return VFS_OK;
}

int vfs_read_all(const vp_ctx_t *ctx, uint32_t privs, const char *spec, const char *deftype,
                 size_t max_bytes, char **buf, size_t *len)
{
    vp_res_t r;
    int rc;

    if (buf == NULL || len == NULL) {
        return VFS_E_ARG;
    }
    rc = resolve(ctx, spec, deftype, VP_R_NEED_NAME, &r);
    if (rc == VFS_OK) {
        rc = norm_dir(&r);
    }
    if (rc == VFS_OK && r.is_dir) {
        rc = VFS_E_ISDIR;
    }
    if (rc == VFS_OK) {
        rc = check_access(ctx, privs, &r, VP_OP_READ);
    }
    if (rc != VFS_OK) {
        return rc;
    }
    if (r.tree == VP_TREE_DEV) {
        return dev_read(&r, max_bytes, buf, len);
    }
    return read_path(&r, max_bytes, buf, len);
}

int vfs_write_all(const vp_ctx_t *ctx, uint32_t privs, const char *spec, const char *deftype,
                  const void *data, size_t len, unsigned flags)
{
    vp_res_t r;
    int rc;

    if (data == NULL && len != 0) {
        return VFS_E_ARG;
    }
    rc = resolve(ctx, spec, deftype, VP_R_NEED_NAME, &r);
    if (rc == VFS_OK && r.tree != VP_TREE_DEV && k_strcmp(r.type, "DIR") == 0) {
        rc = VFS_E_SPEC;   /* "X.DIR" names a folder, never a file */
    }
    if (rc == VFS_OK) {
        rc = check_access(ctx, privs, &r, VP_OP_WRITE);
    }
    if (rc != VFS_OK) {
        return rc;
    }
    if (r.tree == VP_TREE_DEV) {
        return dev_write(&r, data, len, privs);
    }
    rc = stat_res(&r);
    if (rc == VFS_OK) {
        if (st_a.is_dir) {
            return VFS_E_ISDIR;
        }
        if (flags & VFS_W_NEW) {
            return VFS_E_EXISTS;
        }
    } else if (rc != VFS_E_NOTFOUND) {
        return rc;
    }
    return vfs_from_fs(fs_write_file_atomic(r.path, len ? data : "", len), 1);
}

/* ---- Single files and folders ---------------------------------------------------- */

int vfs_delete(const vp_ctx_t *ctx, uint32_t privs, const char *spec)
{
    vp_res_t r;
    int rc = resolve(ctx, spec, NULL, VP_R_NEED_NAME, &r);

    if (rc == VFS_OK) {
        rc = norm_dir(&r);
    }
    if (rc == VFS_OK) {
        rc = check_access(ctx, privs, &r, VP_OP_DELETE);
    }
    if (rc == VFS_OK) {
        rc = stat_res(&r);
    }
    if (rc != VFS_OK) {
        return rc;
    }
    if (st_a.is_dir) {
        return VFS_E_ISDIR;
    }
    return vfs_from_fs(fs_unlink(r.path), 1);
}

int vfs_rename(const vp_ctx_t *ctx, uint32_t privs, const char *from, const char *to)
{
    vp_res_t s, t;
    int rc = resolve(ctx, from, NULL, VP_R_NEED_NAME, &s);

    if (rc == VFS_OK && s.tree == VP_TREE_DEV) {
        rc = VFS_E_NOPRIV;
    }
    if (rc == VFS_OK) {
        rc = check_access(ctx, privs, &s, VP_OP_WRITE);
    }
    if (rc == VFS_OK) {
        rc = stat_res(&s);
    }
    if (rc == VFS_OK && st_a.is_dir) {
        rc = VFS_E_ISDIR;
    }
    if (rc == VFS_OK) {
        rc = target_res(ctx, &s, to, &t);
    }
    if (rc == VFS_OK && t.tree != s.tree) {
        rc = VFS_E_SPEC;
    }
    if (rc == VFS_OK) {
        rc = check_access(ctx, privs, &t, VP_OP_WRITE);
    }
    if (rc != VFS_OK) {
        return rc;
    }
    rc = stat_res(&t);
    if (rc == VFS_OK) {
        return VFS_E_EXISTS;
    }
    if (rc != VFS_E_NOTFOUND) {
        return rc;
    }
    return vfs_from_fs(fs_rename(s.path, t.path), 1);
}

int vfs_mkdir(const vp_ctx_t *ctx, uint32_t privs, const char *spec)
{
    vp_res_t r;
    int rc = resolve(ctx, spec, NULL, 0, &r);

    if (rc == VFS_OK) {
        rc = norm_dir(&r);
    }
    if (rc == VFS_OK && (!r.is_dir || r.tree == VP_TREE_DEV || r.depth == 0)) {
        rc = VFS_E_SPEC;
    }
    if (rc == VFS_OK) {
        rc = check_access(ctx, privs, &r, VP_OP_WRITE);
    }
    if (rc != VFS_OK) {
        return rc;
    }
    rc = vfs_from_fs(fs_stat(r.path, &st_a), 0);
    if (rc == VFS_OK) {
        return VFS_E_EXISTS;
    }
    if (rc != VFS_E_NOTFOUND) {
        return rc;
    }
    /* The parent must be there: missing() of the new folder as a file. */
    r.is_dir = 0;
    rc = missing(&r);
    if (rc != VFS_E_NOTFOUND) {
        return rc;
    }
    return vfs_from_fs(fs_mkdir(r.path), 1);
}

static void fill_entry(const vp_res_t *r, const fs_stat_t *s, vfs_entry_t *e)
{
    k_memset(e, 0, sizeof(*e));
    if (s->is_dir) {
        if (r->depth > 0) {
            k_memcpy(e->name, r->dir[r->depth - 1], sizeof(e->name));
        }
        k_memcpy(e->type, "DIR", 4);
        e->is_dir = 1;
    } else {
        k_memcpy(e->name, r->name, sizeof(e->name));
        k_memcpy(e->type, r->type, sizeof(e->type));
        e->size = s->size;
    }
    e->date = s->date;
    e->time = s->time;
}

static int find(const vp_ctx_t *ctx, uint32_t privs, const char *spec, vp_res_t *r)
{
    int rc = resolve(ctx, spec, NULL, 0, r);

    if (rc == VFS_OK) {
        rc = norm_dir(r);
    }
    if (rc == VFS_OK) {
        rc = check_access(ctx, privs, r, VP_OP_LIST);
    }
    return rc;
}

int vfs_exists(const vp_ctx_t *ctx, uint32_t privs, const char *spec)
{
    vp_res_t r;
    char line[VFS_DEV_LINE];
    int rc = find(ctx, privs, spec, &r);

    if (rc != VFS_OK) {
        return rc;
    }
    if (r.tree == VP_TREE_DEV) {
        if (r.is_dir) {
            return 1;
        }
        if (dev_hooks == NULL || dev_hooks->read == NULL) {
            return VFS_E_NODEV;
        }
        rc = dev_hooks->read(r.path, line, sizeof(line));
        return rc >= 0 ? 1 : (rc == VFS_E_NODEV ? 0 : rc);
    }
    if (r.depth == 0 && r.is_dir) {
        return 1;
    }
    rc = vfs_from_fs(fs_stat(r.path, &st_a), 0);
    if (rc == VFS_OK) {
        return (!r.is_dir || st_a.is_dir) ? 1 : 0;
    }
    return rc == VFS_E_NOTFOUND ? 0 : rc;
}

int vfs_stat(const vp_ctx_t *ctx, uint32_t privs, const char *spec, vfs_entry_t *out)
{
    vp_res_t r;
    int rc;

    if (out == NULL) {
        return VFS_E_ARG;
    }
    rc = find(ctx, privs, spec, &r);
    if (rc == VFS_OK && (r.tree == VP_TREE_DEV || r.wild || (r.is_dir && r.depth == 0))) {
        rc = VFS_E_SPEC;
    }
    if (rc == VFS_OK) {
        rc = stat_res(&r);
    }
    if (rc == VFS_OK && r.is_dir && !st_a.is_dir) {
        rc = VFS_E_DIRNF;
    }
    if (rc != VFS_OK) {
        return rc;
    }
    fill_entry(&r, &st_a, out);
    return VFS_OK;
}

int vfs_free_space(uint64_t *total, uint64_t *free_bytes)
{
    fs_statvfs_t v;
    int rc = vfs_ready();

    if (total == NULL || free_bytes == NULL) {
        return VFS_E_ARG;
    }
    if (rc != VFS_OK) {
        return rc;
    }
    rc = fs_statvfs(VFS_VOLUME, &v);
    if (rc != FS_OK) {
        return vfs_from_fs(rc, 0);
    }
    *total = v.total_bytes;
    *free_bytes = v.free_bytes;
    return VFS_OK;
}

/* ---- Listings -------------------------------------------------------------------- */

/* Walks a folder: calls fn for every entry the filter keeps. */
typedef void (*walk_fn)(const vp_res_t *er, const fs_stat_t *s, void *arg);

static int walk(const vp_ctx_t *ctx, uint32_t privs, const vp_res_t *dir, const char *pname,
                const char *ptype, int op, walk_fn fn, void *arg)
{
    vp_res_t er;
    int rc;

    if (dir->depth > 0) {
        rc = fs_stat(dir->path, &st_a);
        if (rc == FS_ERR_NOENT || (rc == FS_OK && !st_a.is_dir)) {
            return VFS_E_DIRNF;
        }
        if (rc != FS_OK) {
            return vfs_from_fs(rc, 0);
        }
    }
    rc = fs_opendir(&dir_h, dir->path);
    if (rc != FS_OK) {
        return rc == FS_ERR_NOENT ? VFS_E_DIRNF : vfs_from_fs(rc, 0);
    }
    while ((rc = fs_readdir(&dir_h, &st_a)) == 1) {
        if (vfs_list_keep(dir, st_a.name, st_a.is_dir, st_a.leftover, pname, ptype,
                          ctx->home, privs, op, &er)) {
            fn(&er, &st_a, arg);
        }
    }
    (void)fs_closedir(&dir_h);
    return rc < 0 ? vfs_from_fs(rc, 0) : VFS_OK;
}

typedef struct {
    vfs_entry_t *e;
    size_t max, count, total;
} list_acc_t;

static void list_one(const vp_res_t *er, const fs_stat_t *s, void *arg)
{
    list_acc_t *a = arg;
    vfs_entry_t e;

    fill_entry(er, s, &e);
    vfs_list_insert(a->e, &a->count, a->max, &e);
    a->total++;
}

static int list_dev(const vp_res_t *r, const char *pname, const char *ptype, list_acc_t *a)
{
    char name[VP_NAME_MAX + 2];
    vfs_entry_t e;
    unsigned i;
    int rc;

    if (dev_hooks == NULL || dev_hooks->list == NULL) {
        return VFS_E_NODEV;
    }
    (void)r;
    for (i = 0; i < 256u; i++) {
        rc = dev_hooks->list(i, name, sizeof(name));
        if (rc < 0) {
            return VFS_OK;   /* past the last device */
        }
        if (k_strlen(name) > VP_NAME_MAX || !vp_match(pname, ptype, name, "")) {
            continue;
        }
        k_memset(&e, 0, sizeof(e));
        k_memcpy(e.name, name, VP_NAME_MAX + 1);
        vfs_list_insert(a->e, &a->count, a->max, &e);
        a->total++;
    }
    return VFS_OK;
}

int vfs_list(const vp_ctx_t *ctx, uint32_t privs, const char *spec, vfs_entry_t *entries,
             size_t max, size_t *count, size_t *total)
{
    list_acc_t a = { entries, max, 0, 0 };
    vp_spec_t sp;
    vp_res_t r, dir;
    const char *pname, *ptype;
    int rc;

    if (entries == NULL || count == NULL || total == NULL) {
        return VFS_E_ARG;
    }
    *count = 0;
    *total = 0;
    rc = resolve(ctx, spec, NULL, VP_R_WILD_OK | VP_R_EMPTY_OK, &r);
    if (rc != VFS_OK) {
        return rc;
    }
    rc = vp_parse(spec, VP_P_EMPTY_OK | VP_P_WILD_OK, &sp);
    if (rc != VP_OK) {
        return vp_rc(rc);
    }
    vp_list_pattern(&sp, &pname, &ptype);
    if (r.tree == VP_TREE_DEV) {
        rc = check_access(ctx, privs, &r, VP_OP_LIST);
        if (rc == VFS_OK) {
            rc = list_dev(&r, r.is_dir ? "*" : r.path, r.is_dir ? "*" : "", &a);
        }
    } else {
        rc = r.is_dir ? (dir = r, VFS_OK) : folder_of(&r, &dir);
        if (rc == VFS_OK) {
            rc = check_access(ctx, privs, &dir, VP_OP_LIST);
        }
        if (rc == VFS_OK) {
            rc = walk(ctx, privs, &dir, pname, ptype, VP_OP_LIST, list_one, &a);
        }
    }
    *count = a.count;
    *total = a.total;
    return rc;
}

/* ---- Wildcard iterator ---------------------------------------------------------------- */

typedef struct {
    vfs_entry_t *e;
    size_t count, total;
    uint32_t privs;
    const char *home;
    int op;
    uint16_t skipped;
} iter_acc_t;

static void iter_one(const vp_res_t *er, const fs_stat_t *s, void *arg)
{
    iter_acc_t *a = arg;
    vfs_entry_t e;

    if (s->is_dir) {
        return;
    }
    if (vp_access(er, a->home, a->privs, a->op) != VP_OK) {
        a->skipped++;
        return;
    }
    fill_entry(er, s, &e);
    vfs_list_insert(a->e, &a->count, VFS_ITER_MAX, &e);
    a->total++;
}

int vfs_iter_begin(vfs_iter_t *it, const vp_ctx_t *ctx, uint32_t privs, const char *spec, int op)
{
    iter_acc_t a;
    vp_spec_t sp;
    vp_res_t r, dir;
    const char *pname, *ptype;
    char path[VP_PATH_MAX];
    size_t i;
    int rc;

    if (it == NULL) {
        return VFS_E_ARG;
    }
    k_memset(it, 0, sizeof(*it));
    rc = resolve(ctx, spec, NULL, VP_R_WILD_OK | VP_R_NEED_NAME, &r);
    if (rc == VFS_OK && r.tree == VP_TREE_DEV) {
        rc = VFS_E_NOPRIV;
    }
    if (rc == VFS_OK) {
        rc = norm_dir(&r);
    }
    if (rc == VFS_OK && r.is_dir) {
        rc = VFS_E_ISDIR;
    }
    if (rc == VFS_OK) {
        rc = check_access(ctx, privs, &r, op);
    }
    if (rc == VFS_OK) {
        rc = folder_of(&r, &dir);
    }
    if (rc != VFS_OK) {
        return rc;
    }
    if (!r.wild) {
        rc = stat_res(&r);
        if (rc == VFS_OK && st_a.is_dir) {
            rc = VFS_E_ISDIR;
        }
        if (rc != VFS_OK) {
            return rc;
        }
    }
    k_memset(&a, 0, sizeof(a));
    a.e = vfs_alloc(VFS_ITER_MAX * sizeof(vfs_entry_t));
    if (a.e == NULL) {
        return VFS_E_NOMEM;
    }
    a.privs = privs;
    a.home = ctx->home;
    a.op = op;
    pname = r.name;
    ptype = r.type;
    if (r.wild && vp_parse(spec, VP_P_WILD_OK, &sp) == VP_OK) {
        vp_list_pattern(&sp, &pname, &ptype);
    }
    rc = walk(ctx, privs, &dir, pname, ptype, VP_OP_LIST, iter_one, &a);
    if (rc == VFS_OK && a.total > VFS_ITER_MAX) {
        rc = VFS_E_TOOBIG;
    }
    if (rc == VFS_OK && a.count == 0) {
        rc = VFS_E_NOFILES;
    }
    if (rc == VFS_OK) {
        it->spec = vfs_alloc(a.count * VP_DISPLAY_MAX);
        rc = it->spec == NULL ? VFS_E_NOMEM : VFS_OK;
    }
    for (i = 0; rc == VFS_OK && i < a.count; i++) {
        (void)k_snprintf(path, sizeof(path), "%s/%s%s%s", dir.path, a.e[i].name,
                         a.e[i].type[0] != '\0' ? "." : "", a.e[i].type);
        if (vp_format(dir.tree, path, 0, it->spec[i], VP_DISPLAY_MAX) != VP_OK) {
            rc = VFS_E_SPEC;
        }
    }
    it->skipped = a.skipped;
    vfs_free(a.e);
    if (rc != VFS_OK) {
        vfs_iter_end(it);
        it->skipped = a.skipped;
        return rc;
    }
    it->count = (uint16_t)a.count;
    return VFS_OK;
}

const char *vfs_iter_next(vfs_iter_t *it)
{
    if (it == NULL || it->spec == NULL || it->next >= it->count) {
        return NULL;
    }
    return it->spec[it->next++];
}

void vfs_iter_end(vfs_iter_t *it)
{
    if (it != NULL) {
        vfs_free(it->spec);
        it->spec = NULL;
        it->count = 0;
        it->next = 0;
    }
}

/* ---- Stepped copy ------------------------------------------------------------------- */

#define COPY_IDLE 0
#define COPY_RUN  1

static int copy_fail(vfs_copy_t *c, int rc)
{
    vfs_copy_abort(c);
    return rc;
}

int vfs_copy_begin(vfs_copy_t *c, const vp_ctx_t *ctx, uint32_t privs, const char *from,
                   const char *to, unsigned flags)
{
    vp_res_t s, t;
    int rc;

    if (c == NULL) {
        return VFS_E_ARG;
    }
    if (c->state == COPY_RUN) {
        return VFS_E_BUSY;
    }
    k_memset(c, 0, sizeof(*c));
    rc = resolve(ctx, from, NULL, VP_R_NEED_NAME, &s);
    if (rc == VFS_OK && s.tree == VP_TREE_DEV) {
        rc = VFS_E_NOPRIV;
    }
    if (rc == VFS_OK) {
        rc = check_access(ctx, privs, &s, VP_OP_READ);
    }
    if (rc == VFS_OK) {
        rc = stat_res(&s);
    }
    if (rc == VFS_OK && st_a.is_dir) {
        rc = VFS_E_ISDIR;
    }
    if (rc != VFS_OK) {
        return rc;
    }
    c->size = st_a.size;
    rc = target_res(ctx, &s, to, &t);
    if (rc == VFS_OK) {
        rc = check_access(ctx, privs, &t, VP_OP_WRITE);
    }
    if (rc == VFS_OK && k_strcmp(s.path, t.path) == 0) {
        rc = VFS_E_EXISTS;
    }
    if (rc == VFS_OK) {
        rc = stat_res(&t);
        if (rc == VFS_OK) {
            rc = st_a.is_dir ? VFS_E_ISDIR : (flags & VFS_W_NEW) ? VFS_E_EXISTS : VFS_OK;
        } else if (rc == VFS_E_NOTFOUND) {
            rc = VFS_OK;
        }
    }
    if (rc == VFS_OK && (vfs_work_name(t.path, '~', c->work, sizeof(c->work)) != VFS_OK ||
                         vfs_work_name(t.path, '#', c->old, sizeof(c->old)) != VFS_OK)) {
        rc = VFS_E_SPEC;
    }
    if (rc != VFS_OK) {
        return rc;
    }
    k_memcpy(c->to, t.path, sizeof(c->to));
    /* Clears work files an earlier cut left behind, then starts clean. */
    (void)fs_atomic_recover(c->to);
    (void)fs_unlink(c->work);
    c->buf = vfs_alloc(VFS_COPY_CHUNK);
    if (c->buf == NULL) {
        return VFS_E_NOMEM;
    }
    rc = fs_open(&c->in, s.path, FS_READ);
    if (rc != FS_OK) {
        vfs_free(c->buf);
        c->buf = NULL;
        return vfs_from_fs(rc, 0);
    }
    rc = fs_open(&c->out, c->work, FS_WRITE | FS_CREATE | FS_TRUNC);
    if (rc != FS_OK) {
        (void)fs_close(&c->in);
        vfs_free(c->buf);
        c->buf = NULL;
        return vfs_from_fs(rc, 1);
    }
    c->state = COPY_RUN;
    return VFS_OK;
}

/* Moves the finished work file into place, keeping the old file in "#NAME"
 * until the new one has its name. */
static int copy_commit(vfs_copy_t *c)
{
    int had = fs_stat(c->to, &st_b) == FS_OK;
    int rc;

    if (had) {
        rc = fs_rename(c->to, c->old);
        if (rc != FS_OK) {
            return vfs_from_fs(rc, 1);
        }
    }
    rc = fs_rename(c->work, c->to);
    if (rc != FS_OK) {
        if (had) {
            (void)fs_rename(c->old, c->to);
        }
        return vfs_from_fs(rc, 1);
    }
    if (had) {
        (void)fs_unlink(c->old);
    }
    return VFS_OK;
}

int vfs_copy_step(vfs_copy_t *c)
{
    int n, w, rc;

    if (c == NULL || c->state != COPY_RUN) {
        return VFS_E_ARG;
    }
    n = fs_read(&c->in, c->buf, VFS_COPY_CHUNK);
    if (n < 0) {
        return copy_fail(c, vfs_from_fs(n, 0));
    }
    if (n > 0) {
        w = fs_write(&c->out, c->buf, (size_t)n);
        if (w != n) {
            return copy_fail(c, w < 0 ? vfs_from_fs(w, 1) : VFS_E_FULL);
        }
        c->done += (uint32_t)n;
    }
    if (n == (int)VFS_COPY_CHUNK && c->done < c->size) {
        return 1;
    }
    (void)fs_close(&c->in);
    rc = fs_close(&c->out);
    vfs_free(c->buf);
    c->buf = NULL;
    c->state = COPY_IDLE;
    if (rc == FS_OK) {
        rc = copy_commit(c);
    } else {
        rc = vfs_from_fs(rc, 1);
    }
    if (rc != VFS_OK) {
        (void)fs_unlink(c->work);
    }
    return rc;
}

void vfs_copy_abort(vfs_copy_t *c)
{
    if (c == NULL || c->state != COPY_RUN) {
        return;
    }
    (void)fs_close(&c->in);
    (void)fs_close(&c->out);
    (void)fs_unlink(c->work);
    vfs_free(c->buf);
    c->buf = NULL;
    c->state = COPY_IDLE;
}

void vfs_copy_progress(const vfs_copy_t *c, uint32_t *done, uint32_t *size)
{
    *done = c->done;
    *size = c->size;
}

int vfs_copy(const vp_ctx_t *ctx, uint32_t privs, const char *from, const char *to,
             unsigned flags)
{
    static vfs_copy_t c;
    int rc = vfs_copy_begin(&c, ctx, privs, from, to, flags);

    while (rc == VFS_OK && (rc = vfs_copy_step(&c)) == 1) {
    }
    return rc;
}

/* ---- System files ---------------------------------------------------------------------- */

static int sys_res(const char *fat_path, vp_res_t *r)
{
    int rc = vfs_ready();

    if (rc != VFS_OK) {
        return rc;
    }
    if (fat_path == NULL || k_strncmp(fat_path, "/UOS/", 5) != 0 ||
        k_strlen(fat_path) >= sizeof(r->path)) {
        return VFS_E_SPEC;
    }
    k_memset(r, 0, sizeof(*r));
    r->tree = VP_TREE_UOS;
    k_memcpy(r->path, fat_path, k_strlen(fat_path) + 1);
    return VFS_OK;
}

int vfs_sys_read_all(const char *fat_path, size_t max_bytes, char **buf, size_t *len)
{
    vp_res_t r;
    int rc;

    if (buf == NULL || len == NULL) {
        return VFS_E_ARG;
    }
    rc = sys_res(fat_path, &r);
    if (rc != VFS_OK) {
        return rc;
    }
    /* depth 0 keeps missing() from asking about the folder. */
    return read_path(&r, max_bytes, buf, len);
}

int vfs_sys_write_all(const char *fat_path, const void *data, size_t len)
{
    vp_res_t r;
    int rc;

    if (data == NULL && len != 0) {
        return VFS_E_ARG;
    }
    rc = sys_res(fat_path, &r);
    if (rc != VFS_OK) {
        return rc;
    }
    return vfs_from_fs(fs_write_file_atomic(r.path, len ? data : "", len), 1);
}
