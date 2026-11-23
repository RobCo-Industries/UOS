/*
 * OpenVMS-style file specifications mapped to FAT paths (pure).
 *
 * Form: [device:][[directory]]name[.type][;version]
 * See docs/modules/vfs-path.md for the grammar and every rule.
 */
#ifndef UOS_VFS_PATH_H
#define UOS_VFS_PATH_H

#include <stddef.h>
#include <stdint.h>

#include "uos_privs.h"

/* Limits. */
#define VP_NAME_MAX   8    /* name and directory component, characters */
#define VP_TYPE_MAX   3    /* type, characters */
#define VP_DEPTH_MAX  8    /* directory levels below the device root */
#define VP_DEV_MAX    15   /* device or logical name, characters, colon excluded */
#define VP_SPEC_MAX   255  /* input specification, characters */
#define VP_VER_MAX    32767
#define VP_PATH_MAX   96   /* FAT path buffer, terminator included, always enough */
#define VP_DISPLAY_MAX 128 /* formatted specification buffer, always enough */

/* Error codes. 0 is success. */
typedef enum {
    VP_OK = 0,
    VP_E_EMPTY,     /* empty specification where one is required */
    VP_E_TOOLONG,   /* specification longer than VP_SPEC_MAX */
    VP_E_BADCHAR,   /* character not allowed in this position */
    VP_E_NAMELEN,   /* name or directory component over VP_NAME_MAX */
    VP_E_TYPELEN,   /* type over VP_TYPE_MAX */
    VP_E_DEPTH,     /* more than VP_DEPTH_MAX directory levels */
    VP_E_BRACKET,   /* unbalanced or repeated brackets */
    VP_E_BADVER,    /* version is not a number from 0 to VP_VER_MAX */
    VP_E_WILDDIR,   /* wildcard in the device or the directory */
    VP_E_WILD,      /* wildcard where the caller does not accept one */
    VP_E_BADDIR,    /* empty directory component or misplaced "-" */
    VP_E_DEVLEN,    /* device name over VP_DEV_MAX */
    VP_E_NODEV,     /* unknown device or logical name */
    VP_E_ROOT,      /* "-" above the device or logical root */
    VP_E_NONAME,    /* a file name is required */
    VP_E_NOTDIR,    /* a directory is required, the specification names a file */
    VP_E_DEVDIR,    /* DEV: takes no directory */
    VP_E_NOHOME,    /* SYS$LOGIN: with no home in the context */
    VP_E_BUF,       /* output buffer too small */
    VP_E_BADPATH,   /* FAT path outside a known tree or not 8.3 */
    VP_E_NOPRIV,    /* access denied: missing privilege */
    VP_E_READONLY,  /* access denied: the tree is read-only */
    VP_E_PROTECTED, /* access denied: changed only through AUTHORIZE or SET PASSWORD */
    VP_E_COUNT
} vp_err_t;

/* Which tree a resolved specification lives in. */
typedef enum {
    VP_TREE_UOS = 0, /* SD0: and the logical names, FAT /UOS */
    VP_TREE_BOOT,    /* BOOT:, FAT /ROBCO, read-only */
    VP_TREE_DEV      /* DEV:, the device table */
} vp_tree_t;

/* Parse flags. */
#define VP_P_EMPTY_OK 0x01u /* an empty specification parses as no parts */
#define VP_P_WILD_OK  0x02u /* wildcards allowed in name and type */

/* Directory kinds in a parsed specification. */
typedef enum {
    VP_DIR_NONE = 0, /* no brackets */
    VP_DIR_ABS,      /* [A.B] or [000000] */
    VP_DIR_REL       /* [], [.A], [-], [-.A] */
} vp_dirkind_t;

typedef struct {
    char dev[VP_DEV_MAX + 1];                 /* "" when absent */
    uint8_t dir_kind;                         /* vp_dirkind_t */
    uint8_t dir_up;                           /* leading "-" count, relative only */
    uint8_t ndir;                             /* components after the "-" markers */
    char dir[VP_DEPTH_MAX][VP_NAME_MAX + 1];
    char name[VP_NAME_MAX + 1];
    char type[VP_TYPE_MAX + 1];
    uint8_t has_dot;                          /* a "." followed the name */
    uint8_t has_ver;
    uint16_t version;                         /* accepted and ignored */
    uint8_t wild_name;
    uint8_t wild_type;
} vp_spec_t;

/* The caller's context: current default and the user's home folder. */
typedef struct {
    uint8_t tree;                             /* VP_TREE_UOS or VP_TREE_BOOT */
    uint8_t depth;
    char dir[VP_DEPTH_MAX][VP_NAME_MAX + 1];
    char home[VP_NAME_MAX + 1];               /* folder under /UOS/HOME, "" for none */
} vp_ctx_t;

/* Resolve flags. */
#define VP_R_WILD_OK  0x01u /* wildcards allowed in name and type */
#define VP_R_NEED_NAME 0x02u /* a directory-only specification is an error */
#define VP_R_EMPTY_OK 0x04u /* an empty specification means the current directory */

typedef struct {
    uint8_t tree;                             /* vp_tree_t */
    uint8_t depth;                            /* directory levels below the tree root */
    char dir[VP_DEPTH_MAX][VP_NAME_MAX + 1];
    char name[VP_NAME_MAX + 1];               /* "" for a directory-only result */
    char type[VP_TYPE_MAX + 1];
    uint8_t is_dir;                           /* no name: the result is the directory */
    uint8_t wild;                             /* name or type holds a wildcard */
    char path[VP_PATH_MAX];                   /* FAT path, or the device name for DEV: */
} vp_res_t;

/* Directory listing entry. */
typedef struct {
    char name[VP_NAME_MAX + 1];
    char type[VP_TYPE_MAX + 1];               /* "DIR" for a directory */
    uint8_t is_dir;
} vp_entry_t;

/* Access operations and privilege bits (design section 5). */
typedef enum { VP_OP_READ = 0, VP_OP_WRITE, VP_OP_DELETE, VP_OP_LIST } vp_op_t;
#define VP_PRIV_SYSTEM   UOS_PRIV_SYSTEM
#define VP_PRIV_OPER     UOS_PRIV_OPER
#define VP_PRIV_SECURITY UOS_PRIV_SECURITY
#define VP_PRIV_READALL  UOS_PRIV_READALL
_Static_assert(VP_PRIV_SYSTEM == (1u << 0) && VP_PRIV_OPER == (1u << 1) &&
               VP_PRIV_SECURITY == (1u << 2) && VP_PRIV_READALL == (1u << 3),
               "vfs privilege bits match uos_privs.h");

const char *vp_strerror(int err);

int vp_parse(const char *spec, unsigned flags, vp_spec_t *out);

void vp_ctx_init(vp_ctx_t *ctx, const char *home);
int vp_set_default(vp_ctx_t *ctx, const char *spec);
int vp_show_default(const vp_ctx_t *ctx, char *buf, size_t size);

int vp_resolve(const vp_ctx_t *ctx, const char *spec, const char *deftype,
               unsigned flags, vp_res_t *out);

int vp_format(int tree, const char *path, int is_dir, char *buf, size_t size);

int vp_match_part(const char *pattern, const char *s);
int vp_match(const char *pname, const char *ptype, const char *name, const char *type);
void vp_list_pattern(const vp_spec_t *s, const char **pname, const char **ptype);

int vp_entry_from_fat(const char *fatname, int is_dir, vp_entry_t *out);
int vp_entry_cmp(const vp_entry_t *a, const vp_entry_t *b);
void vp_sort_entries(vp_entry_t *e, size_t n);

int vp_access(const vp_res_t *r, const char *home, uint32_t privs, int op);

#endif
