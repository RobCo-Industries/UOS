/*
 * OpenVMS-style file specifications mapped to FAT paths (pure).
 * No hardware, no globals, no allocation. See docs/modules/vfs-path.md.
 */
#include "path.h"

#include <robco/klibc.h>

static const char *const vp_errtext[VP_E_COUNT] = {
    [VP_OK] = "success",
    [VP_E_EMPTY] = "file specification required",
    [VP_E_TOOLONG] = "file specification too long",
    [VP_E_BADCHAR] = "invalid character in file specification",
    [VP_E_NAMELEN] = "file or directory name longer than 8 characters",
    [VP_E_TYPELEN] = "file type longer than 3 characters",
    [VP_E_DEPTH] = "too many directory levels",
    [VP_E_BRACKET] = "unbalanced directory brackets",
    [VP_E_BADVER] = "invalid version number",
    [VP_E_WILDDIR] = "wildcard not allowed in device or directory",
    [VP_E_WILD] = "wildcard not allowed here",
    [VP_E_BADDIR] = "invalid directory syntax",
    [VP_E_DEVLEN] = "device name too long",
    [VP_E_NODEV] = "no such device",
    [VP_E_ROOT] = "directory above the device root",
    [VP_E_NONAME] = "file name required",
    [VP_E_NOTDIR] = "directory required",
    [VP_E_DEVDIR] = "DEV: takes only a device name",
    [VP_E_NOHOME] = "no login directory",
    [VP_E_BUF] = "result too long for the buffer",
    [VP_E_BADPATH] = "path outside the file system",
    [VP_E_NOPRIV] = "insufficient privilege",
    [VP_E_READONLY] = "device is read-only",
    [VP_E_PROTECTED] = "file changes only through AUTHORIZE or SET PASSWORD",
};

const char *vp_strerror(int err)
{
    if (err < 0 || err >= VP_E_COUNT) {
        return "unknown error";
    }
    return vp_errtext[err];
}

static char vp_up(char c)
{
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

/* A-Z 0-9 $ _ - after upper-casing. */
static int vp_namech(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '$' || c == '_' || c == '-';
}

static int vp_wildch(char c)
{
    return c == '*' || c == '%';
}

/* ---------------------------------------------------------------- devices */

typedef struct {
    const char *name;
    uint8_t tree;
    uint8_t logical;      /* 1: rooted logical name */
    const char *root[2];  /* directory components of the logical root */
} vp_devdef_t;

static const vp_devdef_t vp_devs[] = {
    { "SD0", VP_TREE_UOS, 0, { NULL, NULL } },
    { "BOOT", VP_TREE_BOOT, 0, { NULL, NULL } },
    { "DEV", VP_TREE_DEV, 0, { NULL, NULL } },
    { "SYS$SYSTEM", VP_TREE_UOS, 1, { "SYS", NULL } },
    { "SYS$HELP", VP_TREE_UOS, 1, { "SYS", "HELP" } },
    { "SYS$HOLO", VP_TREE_UOS, 1, { "HOLO", NULL } },
    { "SYS$LOGIN", VP_TREE_UOS, 1, { "HOME", NULL } }, /* plus the home folder */
};

#define VP_NDEVS (sizeof(vp_devs) / sizeof(vp_devs[0]))

static const vp_devdef_t *vp_finddev(const char *name)
{
    size_t i;

    for (i = 0; i < VP_NDEVS; i++) {
        if (k_strcmp(vp_devs[i].name, name) == 0) {
            return &vp_devs[i];
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ parse */

static int vp_parse_dir(const char *s, size_t a, size_t b, vp_spec_t *out)
{
    size_t i = a;
    int seen_name = 0;

    /* "..." is the OpenVMS directory wildcard. */
    for (i = a; i + 2 < b; i++) {
        if (s[i] == '.' && s[i + 1] == '.' && s[i + 2] == '.') {
            return VP_E_WILDDIR;
        }
    }
    if (a == b) {
        out->dir_kind = VP_DIR_REL;
        return VP_OK;
    }
    if (b - a == 6 && k_strncmp(s + a, "000000", 6) == 0) {
        out->dir_kind = VP_DIR_ABS;
        return VP_OK;
    }
    i = a;
    if (s[a] == '.') {
        out->dir_kind = VP_DIR_REL;
        i = a + 1;
        seen_name = 1; /* "[.-]" is not a parent marker */
    } else if (s[a] == '-' && (a + 1 == b || s[a + 1] == '.')) {
        out->dir_kind = VP_DIR_REL;
    } else {
        out->dir_kind = VP_DIR_ABS;
    }
    for (;;) {
        size_t start = i;
        size_t len;

        while (i < b && s[i] != '.') {
            char c = vp_up(s[i]);

            if (vp_wildch(c)) {
                return VP_E_WILDDIR;
            }
            if (!vp_namech(c)) {
                return VP_E_BADCHAR;
            }
            i++;
        }
        len = i - start;
        if (len == 0) {
            return VP_E_BADDIR;
        }
        if (len == 1 && s[start] == '-') {
            if (seen_name || out->dir_kind != VP_DIR_REL) {
                return VP_E_BADDIR;
            }
            if (out->dir_up >= VP_DEPTH_MAX) {
                return VP_E_DEPTH;
            }
            out->dir_up++;
        } else {
            size_t k;

            if (len > VP_NAME_MAX) {
                return VP_E_NAMELEN;
            }
            if (out->ndir >= VP_DEPTH_MAX) {
                return VP_E_DEPTH;
            }
            for (k = 0; k < len; k++) {
                out->dir[out->ndir][k] = vp_up(s[start + k]);
            }
            out->dir[out->ndir][len] = '\0';
            out->ndir++;
            seen_name = 1;
        }
        if (i == b) {
            return VP_OK;
        }
        i++; /* the dot */
        if (i == b) {
            return VP_E_BADDIR; /* trailing dot */
        }
    }
}

/* Name or type field: stops at any of the given stop characters. */
static int vp_parse_field(const char *s, size_t *pi, size_t n, char *dst, size_t max,
                          int toolong, uint8_t *wild, int stop_dot)
{
    size_t i = *pi;
    size_t len = 0;

    while (i < n && s[i] != ';' && !(stop_dot && s[i] == '.')) {
        char c = vp_up(s[i]);

        if (c == '[' || c == ']') {
            return VP_E_BRACKET;
        }
        if (vp_wildch(c)) {
            *wild = 1;
        } else if (!vp_namech(c)) {
            return VP_E_BADCHAR;
        }
        if (len >= max) {
            return toolong;
        }
        dst[len++] = c;
        i++;
    }
    dst[len] = '\0';
    *pi = i;
    return VP_OK;
}

int vp_parse(const char *spec, unsigned flags, vp_spec_t *out)
{
    size_t n = 0;
    size_t i = 0;
    size_t colon = 0;
    size_t lb = 0;
    int has_colon = 0;
    int has_lb = 0;
    int rc;

    k_memset(out, 0, sizeof(*out));
    if (spec == NULL) {
        return (flags & VP_P_EMPTY_OK) ? VP_OK : VP_E_EMPTY;
    }
    while (spec[n] != '\0') {
        if (n >= VP_SPEC_MAX) {
            return VP_E_TOOLONG;
        }
        if (spec[n] == ':' && !has_colon) {
            has_colon = 1;
            colon = n;
        }
        if (spec[n] == '[' && !has_lb) {
            has_lb = 1;
            lb = n;
        }
        n++;
    }
    if (n == 0) {
        return (flags & VP_P_EMPTY_OK) ? VP_OK : VP_E_EMPTY;
    }

    /* Device: the text before the first colon, when it comes before any bracket. */
    if (has_colon && (!has_lb || colon < lb)) {
        if (colon == 0) {
            return VP_E_BADCHAR;
        }
        for (i = 0; i < colon; i++) {
            char c = vp_up(spec[i]);

            if (vp_wildch(c)) {
                return VP_E_WILDDIR;
            }
            if (c == ']') {
                return VP_E_BRACKET;
            }
            if (!vp_namech(c) || c == '-') {
                return VP_E_BADCHAR;
            }
        }
        if (colon > VP_DEV_MAX) {
            return VP_E_DEVLEN;
        }
        for (i = 0; i < colon; i++) {
            out->dev[i] = vp_up(spec[i]);
        }
        out->dev[colon] = '\0';
        if (vp_finddev(out->dev) == NULL) {
            return VP_E_NODEV;
        }
        i = colon + 1;
    }

    /* Directory. */
    if (i < n && spec[i] == '[') {
        size_t a = i + 1;
        size_t b = a;

        while (b < n && spec[b] != ']') {
            if (spec[b] == '[') {
                return VP_E_BRACKET;
            }
            b++;
        }
        if (b == n) {
            return VP_E_BRACKET;
        }
        rc = vp_parse_dir(spec, a, b, out);
        if (rc != VP_OK) {
            return rc;
        }
        i = b + 1;
    }

    /* Name, type, version. */
    rc = vp_parse_field(spec, &i, n, out->name, VP_NAME_MAX, VP_E_NAMELEN, &out->wild_name, 1);
    if (rc != VP_OK) {
        return rc;
    }
    if (i < n && spec[i] == '.') {
        out->has_dot = 1;
        i++;
        rc = vp_parse_field(spec, &i, n, out->type, VP_TYPE_MAX, VP_E_TYPELEN, &out->wild_type, 1);
        if (rc != VP_OK) {
            return rc;
        }
        if (i < n && spec[i] == '.') {
            return VP_E_BADCHAR;
        }
    }
    if (i < n && spec[i] == ';') {
        uint32_t v = 0;

        out->has_ver = 1;
        i++;
        while (i < n) {
            if (spec[i] < '0' || spec[i] > '9') {
                return VP_E_BADVER;
            }
            v = v * 10 + (uint32_t)(spec[i] - '0');
            if (v > VP_VER_MAX) {
                return VP_E_BADVER;
            }
            i++;
        }
        out->version = (uint16_t)v;
    }
    if ((out->wild_name || out->wild_type) && !(flags & VP_P_WILD_OK)) {
        return VP_E_WILD;
    }
    return VP_OK;
}

/* ---------------------------------------------------------------- context */

void vp_ctx_init(vp_ctx_t *ctx, const char *home)
{
    size_t i;

    k_memset(ctx, 0, sizeof(*ctx));
    ctx->tree = VP_TREE_UOS;
    if (home == NULL) {
        return;
    }
    for (i = 0; i < VP_NAME_MAX && home[i] != '\0'; i++) {
        if (!vp_namech(vp_up(home[i]))) {
            ctx->home[0] = '\0';
            return;
        }
        ctx->home[i] = vp_up(home[i]);
    }
    if (home[i] != '\0') {
        ctx->home[0] = '\0';
        return;
    }
    ctx->home[i] = '\0';
    if (ctx->home[0] != '\0') {
        k_strncpy(ctx->dir[0], "HOME", sizeof(ctx->dir[0]));
        k_strncpy(ctx->dir[1], ctx->home, sizeof(ctx->dir[1]));
        ctx->depth = 2;
    }
}

/* Appends text; returns VP_E_BUF when it does not fit with the terminator. */
static int vp_put(char *buf, size_t size, size_t *pos, const char *s)
{
    size_t len = k_strlen(s);

    if (*pos + len + 1 > size) {
        return VP_E_BUF;
    }
    k_memcpy(buf + *pos, s, len);
    *pos += len;
    buf[*pos] = '\0';
    return VP_OK;
}

static int vp_put_dirs(char *buf, size_t size, size_t *pos, char dir[][VP_NAME_MAX + 1],
                       unsigned depth)
{
    unsigned d;

    if (vp_put(buf, size, pos, "[") != VP_OK) {
        return VP_E_BUF;
    }
    if (depth == 0 && vp_put(buf, size, pos, "000000") != VP_OK) {
        return VP_E_BUF;
    }
    for (d = 0; d < depth; d++) {
        if ((d > 0 && vp_put(buf, size, pos, ".") != VP_OK) ||
            vp_put(buf, size, pos, dir[d]) != VP_OK) {
            return VP_E_BUF;
        }
    }
    return vp_put(buf, size, pos, "]");
}

static const char *vp_treedev(int tree)
{
    return tree == VP_TREE_BOOT ? "BOOT:" : tree == VP_TREE_DEV ? "DEV:" : "SD0:";
}

int vp_show_default(const vp_ctx_t *ctx, char *buf, size_t size)
{
    size_t pos = 0;
    vp_ctx_t c = *ctx;

    if (size > 0) {
        buf[0] = '\0';
    }
    if (vp_put(buf, size, &pos, vp_treedev(c.tree)) != VP_OK ||
        vp_put_dirs(buf, size, &pos, c.dir, c.depth) != VP_OK) {
        if (size > 0) {
            buf[0] = '\0';
        }
        return VP_E_BUF;
    }
    return VP_OK;
}

int vp_set_default(vp_ctx_t *ctx, const char *spec)
{
    vp_res_t r;
    int rc = vp_resolve(ctx, spec, NULL, 0, &r);

    if (rc != VP_OK) {
        return rc;
    }
    if (!r.is_dir || r.tree == VP_TREE_DEV) {
        return VP_E_NOTDIR;
    }
    ctx->tree = r.tree;
    ctx->depth = r.depth;
    k_memcpy(ctx->dir, r.dir, sizeof(ctx->dir));
    return VP_OK;
}

/* ---------------------------------------------------------------- resolve */

static int vp_build_path(vp_res_t *r)
{
    size_t pos = 0;
    unsigned d;
    char *b = r->path;
    size_t sz = sizeof(r->path);

    b[0] = '\0';
    if (r->tree == VP_TREE_DEV) {
        return vp_put(b, sz, &pos, r->name);
    }
    if (vp_put(b, sz, &pos, r->tree == VP_TREE_BOOT ? "/ROBCO" : "/UOS") != VP_OK) {
        return VP_E_BUF;
    }
    for (d = 0; d < r->depth; d++) {
        if (vp_put(b, sz, &pos, "/") != VP_OK || vp_put(b, sz, &pos, r->dir[d]) != VP_OK) {
            return VP_E_BUF;
        }
    }
    if (r->is_dir || r->wild) {
        return VP_OK;
    }
    if (vp_put(b, sz, &pos, "/") != VP_OK || vp_put(b, sz, &pos, r->name) != VP_OK) {
        return VP_E_BUF;
    }
    if (r->type[0] != '\0' &&
        (vp_put(b, sz, &pos, ".") != VP_OK || vp_put(b, sz, &pos, r->type) != VP_OK)) {
        return VP_E_BUF;
    }
    return VP_OK;
}

static int vp_push(vp_res_t *r, const char *comp)
{
    if (r->depth >= VP_DEPTH_MAX) {
        return VP_E_DEPTH;
    }
    k_strncpy(r->dir[r->depth], comp, sizeof(r->dir[0]));
    r->dir[r->depth][VP_NAME_MAX] = '\0';
    r->depth++;
    return VP_OK;
}

int vp_resolve(const vp_ctx_t *ctx, const char *spec, const char *deftype,
               unsigned flags, vp_res_t *out)
{
    vp_spec_t s;
    const vp_devdef_t *dev = NULL;
    unsigned pflags = 0;
    unsigned floor = 0;
    unsigned d;
    int rc;

    k_memset(out, 0, sizeof(*out));
    if (flags & VP_R_WILD_OK) {
        pflags |= VP_P_WILD_OK;
    }
    if (flags & VP_R_EMPTY_OK) {
        pflags |= VP_P_EMPTY_OK;
    }
    rc = vp_parse(spec, pflags, &s);
    if (rc != VP_OK) {
        return rc;
    }
    if (s.dev[0] != '\0') {
        dev = vp_finddev(s.dev);
    }

    if (dev != NULL && dev->tree == VP_TREE_DEV) {
        out->tree = VP_TREE_DEV;
        if (s.dir_kind != VP_DIR_NONE || s.has_dot || s.wild_name) {
            return VP_E_DEVDIR;
        }
        if (s.name[0] == '\0') {
            if (s.has_ver || (flags & VP_R_NEED_NAME)) {
                return VP_E_NONAME;
            }
            out->is_dir = 1;
            return VP_OK;
        }
        k_strncpy(out->name, s.name, sizeof(out->name));
        return vp_build_path(out);
    }

    /* Base: the tree root, a logical root, or the current directory. */
    if (dev == NULL) {
        out->tree = ctx->tree;
    } else {
        out->tree = dev->tree;
    }
    if (dev != NULL && dev->logical) {
        for (d = 0; d < 2 && dev->root[d] != NULL; d++) {
            vp_push(out, dev->root[d]);
        }
        if (k_strcmp(dev->name, "SYS$LOGIN") == 0) {
            if (ctx->home[0] == '\0') {
                return VP_E_NOHOME;
            }
            vp_push(out, ctx->home);
        }
        floor = out->depth;
    } else if (s.dir_kind != VP_DIR_ABS && out->tree == ctx->tree) {
        out->depth = ctx->depth;
        k_memcpy(out->dir, ctx->dir, sizeof(out->dir));
    }
    if (s.dir_kind == VP_DIR_REL || (dev != NULL && dev->logical)) {
        for (d = 0; d < s.dir_up; d++) {
            if (out->depth <= floor) {
                return VP_E_ROOT;
            }
            out->depth--;
        }
    }
    for (d = 0; d < s.ndir; d++) {
        rc = vp_push(out, s.dir[d]);
        if (rc != VP_OK) {
            return rc;
        }
    }

    if (s.name[0] == '\0') {
        if (s.has_dot || s.has_ver || (flags & VP_R_NEED_NAME)) {
            return VP_E_NONAME;
        }
        out->is_dir = 1;
    } else {
        k_strncpy(out->name, s.name, sizeof(out->name));
        if (s.has_dot) {
            k_strncpy(out->type, s.type, sizeof(out->type));
        } else if (deftype != NULL) {
            size_t k;

            for (k = 0; k < VP_TYPE_MAX && deftype[k] != '\0'; k++) {
                out->type[k] = vp_up(deftype[k]);
            }
            out->type[k] = '\0';
        }
        out->wild = (uint8_t)(s.wild_name || s.wild_type);
    }
    return vp_build_path(out);
}

/* ----------------------------------------------------------------- format */

/* Checks a name.type component; is_dir forbids the dot. */
static int vp_check_comp(const char *p, size_t len, int last, int is_dir)
{
    size_t i;
    size_t name = 0;
    size_t type = 0;
    int dot = 0;

    for (i = 0; i < len; i++) {
        char c = vp_up(p[i]);

        if (c == '.') {
            if (dot || !last || is_dir) {
                return 0;
            }
            dot = 1;
        } else if (!vp_namech(c)) {
            return 0;
        } else if (dot) {
            type++;
        } else {
            name++;
        }
    }
    return name >= 1 && name <= VP_NAME_MAX && type <= VP_TYPE_MAX;
}

int vp_format(int tree, const char *path, int is_dir, char *buf, size_t size)
{
    const char *root = tree == VP_TREE_BOOT ? "/ROBCO" : "/UOS";
    size_t rl = k_strlen(root);
    size_t pos = 0;
    size_t i;
    size_t last = 0;
    unsigned ncomp = 0;
    int rc = VP_E_BUF;

    if (size > 0) {
        buf[0] = '\0';
    }
    if (path == NULL || tree < VP_TREE_UOS || tree > VP_TREE_DEV) {
        return VP_E_BADPATH;
    }
    if (tree == VP_TREE_DEV) {
        size_t n = k_strlen(path);

        if (n == 0 || n > VP_NAME_MAX || !vp_check_comp(path, n, 1, 1)) {
            return VP_E_BADPATH;
        }
        if (vp_put(buf, size, &pos, "DEV:") == VP_OK) {
            for (i = 0; i < n; i++) {
                char c[2] = { vp_up(path[i]), '\0' };

                if (vp_put(buf, size, &pos, c) != VP_OK) {
                    goto fail;
                }
            }
            rc = vp_put(buf, size, &pos, ";1");
        }
        goto done;
    }
    if (k_strncmp(path, root, rl) != 0 || (path[rl] != '\0' && path[rl] != '/')) {
        return VP_E_BADPATH;
    }
    /* Validate the components and find the last one. */
    i = rl;
    while (path[i] == '/') {
        size_t a = i + 1;
        size_t b = a;

        while (path[b] != '\0' && path[b] != '/') {
            b++;
        }
        if (!vp_check_comp(path + a, b - a, path[b] == '\0', is_dir)) {
            return VP_E_BADPATH;
        }
        ncomp++;
        last = a;
        i = b;
    }
    if (ncomp == 0) {
        if (!is_dir) {
            return VP_E_BADPATH;
        }
        rc = vp_put(buf, size, &pos, vp_treedev(tree));
        if (rc == VP_OK) {
            rc = vp_put(buf, size, &pos, "[000000]000000.DIR;1");
        }
        goto done;
    }
    if (ncomp - 1 > VP_DEPTH_MAX) {
        return VP_E_BADPATH;
    }
    if (vp_put(buf, size, &pos, vp_treedev(tree)) != VP_OK ||
        vp_put(buf, size, &pos, "[") != VP_OK) {
        goto fail;
    }
    if (ncomp == 1 && vp_put(buf, size, &pos, "000000") != VP_OK) {
        goto fail;
    }
    for (i = rl + 1; path[i] != '\0'; i++) {
        char c[2] = { vp_up(path[i]), '\0' };

        if (i == last) {
            break;
        }
        if (c[0] == '/') {
            c[0] = i + 1 == last ? ']' : '.';
        }
        if (vp_put(buf, size, &pos, c) != VP_OK) {
            goto fail;
        }
    }
    if (ncomp == 1 && vp_put(buf, size, &pos, "]") != VP_OK) {
        goto fail;
    }
    {
        int dot = 0;

        for (i = last; path[i] != '\0'; i++) {
            char c[2] = { vp_up(path[i]), '\0' };

            dot |= c[0] == '.';
            if (vp_put(buf, size, &pos, c) != VP_OK) {
                goto fail;
            }
        }
        if (is_dir) {
            rc = vp_put(buf, size, &pos, ".DIR;1");
        } else {
            rc = vp_put(buf, size, &pos, dot ? ";1" : ".;1");
        }
    }
done:
    if (rc == VP_OK) {
        return VP_OK;
    }
fail:
    if (size > 0) {
        buf[0] = '\0';
    }
    return VP_E_BUF;
}

/* --------------------------------------------------------------- wildcard */

int vp_match_part(const char *p, const char *s)
{
    while (*p != '\0') {
        if (*p == '*') {
            while (*p == '*') {
                p++;
            }
            if (*p == '\0') {
                return 1;
            }
            for (;;) {
                if (vp_match_part(p, s)) {
                    return 1;
                }
                if (*s == '\0') {
                    return 0;
                }
                s++;
            }
        }
        if (*s == '\0') {
            return 0;
        }
        if (*p != '%' && vp_up(*p) != vp_up(*s)) {
            return 0;
        }
        p++;
        s++;
    }
    return *s == '\0';
}

int vp_match(const char *pname, const char *ptype, const char *name, const char *type)
{
    return vp_match_part(pname, name) && vp_match_part(ptype, type);
}

void vp_list_pattern(const vp_spec_t *s, const char **pname, const char **ptype)
{
    *pname = s->name[0] != '\0' ? s->name : "*";
    *ptype = s->has_dot ? s->type : "*";
}

/* ---------------------------------------------------------------- entries */

int vp_entry_from_fat(const char *fatname, int is_dir, vp_entry_t *out)
{
    size_t n;
    size_t i;
    size_t j = 0;
    int dot = 0;

    k_memset(out, 0, sizeof(*out));
    if (fatname == NULL) {
        return VP_E_BADPATH;
    }
    n = k_strlen(fatname);
    if (n == 0 || n > VP_NAME_MAX + 1 + VP_TYPE_MAX || !vp_check_comp(fatname, n, 1, is_dir)) {
        return VP_E_BADPATH;
    }
    for (i = 0; i < n; i++) {
        if (fatname[i] == '.') {
            out->name[j] = '\0';
            dot = 1;
            j = 0;
            continue;
        }
        if (dot) {
            out->type[j++] = vp_up(fatname[i]);
        } else {
            out->name[j++] = vp_up(fatname[i]);
        }
    }
    if (is_dir) {
        k_strncpy(out->type, "DIR", sizeof(out->type));
        out->is_dir = 1;
    }
    return VP_OK;
}

int vp_entry_cmp(const vp_entry_t *a, const vp_entry_t *b)
{
    int c = k_strcmp(a->name, b->name);

    return c != 0 ? c : k_strcmp(a->type, b->type);
}

void vp_sort_entries(vp_entry_t *e, size_t n)
{
    size_t i;

    for (i = 1; i < n; i++) {
        vp_entry_t t = e[i];
        size_t j = i;

        while (j > 0 && vp_entry_cmp(&e[j - 1], &t) > 0) {
            e[j] = e[j - 1];
            j--;
        }
        e[j] = t;
    }
}

/* ----------------------------------------------------------------- access */

int vp_access(const vp_res_t *r, const char *home, uint32_t privs, int op)
{
    int reading = op == VP_OP_READ || op == VP_OP_LIST;
    unsigned k = r->depth + (r->is_dir ? 0u : 1u);
    const char *c0 = r->depth > 0 ? r->dir[0] : r->name;

    if (op < VP_OP_READ || op > VP_OP_LIST) {
        return VP_E_NOPRIV;
    }
    if (r->tree == VP_TREE_DEV) {
        return op == VP_OP_DELETE ? VP_E_NOPRIV : VP_OK;
    }
    if (r->tree == VP_TREE_BOOT) {
        if (!reading) {
            return VP_E_READONLY;
        }
        return (privs & VP_PRIV_SYSTEM) ? VP_OK : VP_E_NOPRIV;
    }
    if (r->tree != VP_TREE_UOS) {
        return VP_E_NOPRIV;
    }
    if (k == 1 && !r->is_dir && k_strcmp(r->name, "SYSUAF") == 0 &&
        k_strcmp(r->type, "DAT") == 0) {
        if (!reading) {
            return VP_E_PROTECTED;
        }
        return (privs & (VP_PRIV_SYSTEM | VP_PRIV_SECURITY)) ? VP_OK : VP_E_NOPRIV;
    }
    if (privs & VP_PRIV_SYSTEM) {
        return VP_OK;
    }
    if (r->depth == 0) {
        return VP_E_NOPRIV; /* the root and files in it */
    }
    if (k_strcmp(c0, "SYS") == 0 || k_strcmp(c0, "HOLO") == 0) {
        return reading ? VP_OK : VP_E_NOPRIV;
    }
    if (k_strcmp(c0, "HOME") == 0) {
        if (k == 1) {
            return (op == VP_OP_LIST && (privs & VP_PRIV_READALL)) ? VP_OK : VP_E_NOPRIV;
        }
        if (home != NULL && home[0] != '\0' && k_strcmp(r->dir[1], home) == 0) {
            if (k == 2) {
                return reading ? VP_OK : VP_E_NOPRIV;
            }
            return VP_OK;
        }
        return (reading && (privs & VP_PRIV_READALL)) ? VP_OK : VP_E_NOPRIV;
    }
    return VP_E_NOPRIV;
}
