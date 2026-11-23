/*
 * The file system service: one way for commands and screens to reach
 * files on the card through file specifications. Prefix vfs_.
 *
 * Every call that touches the card resolves the specification with the
 * caller's context (src/vfs/path.h), checks access with vp_access(), acts
 * through the library's FatFs glue, and returns 0 or one negative VFS_E_*
 * code. vfs_msg() maps a code to the command language's message.
 *
 * Context rules, for every function below unless it says otherwise:
 *   "loop": the main loop only, from a screen callback, a posted call, a
 *   timer call or a service's init. Never from an IRQ handler, a
 *   maintenance command function or the panic path: the library's file
 *   system has no locking and a card operation can take many ticks.
 *   "any": pure, any context.
 *
 * Buffers: the caller owns every buffer it passes. vfs_read_all() and
 * vfs_sys_read_all() hand back a heap buffer that the caller frees with
 * vfs_free().
 *
 * Not ready: when the kernel found no card, every card call returns
 * VFS_E_NOCARD; with a card but no /UOS folder, VFS_E_NOTREE.
 */
#ifndef UOS_VFS_H
#define UOS_VFS_H

#include <stddef.h>
#include <stdint.h>

#include <robco/fs.h>

#include "dcl.h"
#include "path.h"

/* Error codes. 0 is success; every error is negative. */
#define VFS_OK          0
#define VFS_E_NOCARD    (-1)  /* no card mounted */
#define VFS_E_NOTREE    (-2)  /* a card, but no /UOS folder */
#define VFS_E_SPEC      (-3)  /* bad file specification: see vfs_last_vp() */
#define VFS_E_NOTFOUND  (-4)  /* file not found */
#define VFS_E_DIRNF     (-5)  /* directory not found */
#define VFS_E_ISDIR     (-6)  /* a directory where a file is expected */
#define VFS_E_EXISTS    (-7)  /* the target exists and the caller said it must not */
#define VFS_E_NOPRIV    (-8)  /* the access table refuses it */
#define VFS_E_READONLY  (-9)  /* BOOT: is read-only */
#define VFS_E_PROTECTED (-10) /* SYSUAF.DAT: only AUTHORIZE or SET PASSWORD */
#define VFS_E_TOOBIG    (-11) /* the file is over the caller's limit */
#define VFS_E_READ      (-12) /* the card failed while reading */
#define VFS_E_WRITE     (-13) /* the card failed while writing, or refused the change */
#define VFS_E_FULL      (-14) /* no space left on the card */
#define VFS_E_NOMEM     (-15) /* the heap has no room for the buffer */
#define VFS_E_NODEV     (-16) /* DEV: with no hook, or the hook knows no such device */
#define VFS_E_NOFILES   (-17) /* a wildcard matched no file the caller may use */
#define VFS_E_BUSY      (-18) /* a stepped copy is already running in this handle */
#define VFS_E_ARG       (-19) /* bad argument: NULL pointer, zero limit */
#define VFS_E_COUNT     20    /* number of codes, VFS_OK included */

/* Short lower-case word for markers, such as "notfound". Any. */
const char *vfs_strerror(int err);
/* The command language's message for a code. VFS_OK gives DCL_NORMAL. Any. */
dcl_msg_t vfs_msg(int err);
/* The vp_err_t behind the last VFS_E_SPEC or access refusal. Loop. */
int vfs_last_vp(void);

/* ---- Listing entries ---------------------------------------------------- */

/* A FAT date and time as "DD-MMM-YYYY HH:MM:SS", 20 characters. */
#define VFS_DATE_LEN 20

typedef struct {
    char name[VP_NAME_MAX + 1];
    char type[VP_TYPE_MAX + 1];   /* "DIR" for a directory */
    uint8_t is_dir;
    uint32_t size;                /* bytes; 0 for a directory or a DEV: entry */
    uint16_t date, time;          /* FAT format: date (year-1980)<<9 | month<<5 | day,
                                   * time hour<<11 | minute<<5 | second/2; 0 for DEV: */
} vfs_entry_t;

/* ---- Service state -------------------------------------------------------- */

/* VFS_OK when the card and /UOS are there, else VFS_E_NOCARD or VFS_E_NOTREE. Any. */
int vfs_ready(void);

/* ---- Whole files ------------------------------------------------------- */

/*
 * Reads a whole file into a new heap buffer with one extra NUL after the
 * data. *buf and *len are set only on success; the caller frees *buf with
 * vfs_free(). A file larger than max_bytes is VFS_E_TOOBIG and nothing is
 * read. deftype, may be NULL, is the type a name without a dot takes. DEV:
 * names go through the registered hook. Time: one pass over the file, so
 * the caller's max_bytes bounds it. Loop.
 */
int vfs_read_all(const vp_ctx_t *ctx, uint32_t privs, const char *spec, const char *deftype,
                 size_t max_bytes, char **buf, size_t *len);

/* Frees a buffer from vfs_read_all() or vfs_sys_read_all(). NULL does nothing. Loop. */
void vfs_free(void *buf);

/* vfs_write_all() flags. */
#define VFS_W_NEW 0x01u /* the target must not exist: VFS_E_EXISTS */

/*
 * Writes a whole file with the library's fs_write_file_atomic(): the data
 * goes to "~NAME" in the same folder, then replaces NAME. After a power
 * cut the target holds the whole old or the whole new content, or is
 * absent for a first write; the library finishes or undoes the work files
 * on the next read or write of that target. A full card is VFS_E_FULL and
 * leaves the old file as it was. DEV: names pass `data` to the hook's
 * write as text. Loop.
 */
int vfs_write_all(const vp_ctx_t *ctx, uint32_t privs, const char *spec, const char *deftype,
                  const void *data, size_t len, unsigned flags);

/* ---- Single files and folders --------------------------------------------- */

/* Deletes one file. No wildcard: use vfs_iter_*. A folder is VFS_E_ISDIR. Loop. */
int vfs_delete(const vp_ctx_t *ctx, uint32_t privs, const char *spec);

/*
 * Renames one file, also to another folder of the same tree. `to` may be a
 * folder only, or hold "*" as its name or type: those parts take the
 * source's. The target must not exist: VFS_E_EXISTS. Loop.
 */
int vfs_rename(const vp_ctx_t *ctx, uint32_t privs, const char *from, const char *to);

/* Creates a folder: "[.NEW]" or "NEW.DIR". VFS_E_EXISTS when the name exists. Loop. */
int vfs_mkdir(const vp_ctx_t *ctx, uint32_t privs, const char *spec);

/* 1 when the file or folder exists, 0 when not, or a negative code. Loop. */
int vfs_exists(const vp_ctx_t *ctx, uint32_t privs, const char *spec);

/* Fills *out for one file or folder ("[.SUB]" or "SUB.DIR"). Needs list access. Loop. */
int vfs_stat(const vp_ctx_t *ctx, uint32_t privs, const char *spec, vfs_entry_t *out);

/* Size and free bytes of the card's volume. Scans the FAT once. Loop. */
int vfs_free_space(uint64_t *total, uint64_t *free_bytes);

/* ---- Listings ---------------------------------------------------------- */

/*
 * Lists a folder: "[.MAIL]", "*.TXT", "A%.*", "" for the current folder,
 * "DEV:" for the devices. Fills entries[0..*count) with the first `max`
 * entries in the path module's order (name, then type) and sets *total to
 * every entry that matched, so *total > *count means more. Hidden: the
 * library's work files ("~" and "#" names), names that are not 8.3, and
 * every entry the access table does not let the caller list. Loop.
 */
int vfs_list(const vp_ctx_t *ctx, uint32_t privs, const char *spec, vfs_entry_t *entries,
             size_t max, size_t *count, size_t *total);

/* ---- Wildcard iterator -------------------------------------------------- */

/* Matches one iterator holds, at most. More is VFS_E_TOOBIG at begin. */
#define VFS_ITER_MAX 128

typedef struct {
    char (*spec)[VP_DISPLAY_MAX];   /* private: heap array of matches */
    uint16_t count, next;
    uint16_t skipped;               /* matches the access table refused */
} vfs_iter_t;

/*
 * Resolves a specification, with or without wildcards, to its files in
 * the path module's order. Folders never match. Each file is checked
 * again with vp_access() for `op` (VP_OP_READ, VP_OP_WRITE or
 * VP_OP_DELETE); refused files are counted in `skipped` and left out.
 * No match left is VFS_E_NOFILES. On success the caller calls
 * vfs_iter_end() once. Loop.
 */
int vfs_iter_begin(vfs_iter_t *it, const vp_ctx_t *ctx, uint32_t privs, const char *spec,
                   int op);

/*
 * The next match as an absolute specification such as
 * "SD0:[HOME.ADMIN]A.TXT;1", valid until vfs_iter_end(). Returns NULL at
 * the end. Pass it to vfs_delete(), vfs_rename() or vfs_copy_begin(),
 * which check access again. Any.
 */
const char *vfs_iter_next(vfs_iter_t *it);

/* Frees the iterator's matches. Safe twice. Loop. */
void vfs_iter_end(vfs_iter_t *it);

/* ---- Stepped copy --------------------------------------------------------- */

/* Bytes one vfs_copy_step() moves at most: 32 sectors read, 32 written. */
#define VFS_COPY_CHUNK 16384u

/* Caller-owned copy state, about 1.7 KiB: keep it static, not on the stack.
 * The fields are private. */
typedef struct {
    fs_file_t in, out;
    void *buf;
    uint32_t size, done;
    uint8_t state;
    char to[VP_PATH_MAX];
    char work[VP_PATH_MAX + 1];   /* "~NAME" */
    char old[VP_PATH_MAX + 1];    /* "#NAME" */
} vfs_copy_t;

/*
 * Starts a copy of one file. `to` may be a folder only or hold "*" parts,
 * as for vfs_rename(). With VFS_W_NEW an existing target is VFS_E_EXISTS;
 * otherwise the copy replaces it. Opens the source, writes to "~NAME" next
 * to the target and allocates a VFS_COPY_CHUNK heap buffer. Loop.
 */
int vfs_copy_begin(vfs_copy_t *c, const vp_ctx_t *ctx, uint32_t privs, const char *from,
                   const char *to, unsigned flags);

/*
 * Moves at most VFS_COPY_CHUNK bytes. Returns 1 while bytes remain, 0 when
 * the copy is complete and in place, or a negative code; after 0 or an
 * error the copy is finished and its buffer and work file are gone. The
 * last step moves the target as fs_write_file_atomic() does, through
 * "#NAME", so a power cut leaves the old or the new file. Loop.
 */
int vfs_copy_step(vfs_copy_t *c);

/* Stops a running copy and removes its work file. Safe when finished. Loop. */
void vfs_copy_abort(vfs_copy_t *c);

/* Bytes copied so far and the source's size. Any. */
void vfs_copy_progress(const vfs_copy_t *c, uint32_t *done, uint32_t *size);

/*
 * The whole copy in one call: begin, then steps until done. Its time grows
 * with the file: one step per VFS_COPY_CHUNK bytes. Use it for files the
 * caller knows are small; post vfs_copy_step() calls to the loop for the
 * rest. Loop.
 */
int vfs_copy(const vp_ctx_t *ctx, uint32_t privs, const char *from, const char *to,
             unsigned flags);

/* ---- DEV: pseudo-files ---------------------------------------------------- */

/*
 * The device service registers these at its init. Each returns as below,
 * or a negative VFS_E_* code (VFS_E_NODEV for an unknown name). Called
 * from the loop only.
 *   read(name, buf, size): writes the device's status line, NUL ended,
 *     and returns its length.
 *   write(name, text): acts on the command word in text; 0 or a code.
 *   list(index, name, size): 1 with the index-th device name, 0 past the end.
 */
typedef struct {
    int (*read)(const char *name, char *buf, size_t size);
    int (*write)(const char *name, const char *text);
    int (*list)(unsigned index, char *name, size_t size);
} vfs_dev_hooks_t;

/* Registers the hooks; NULL removes them. The struct must stay valid. Loop. */
void vfs_dev_register(const vfs_dev_hooks_t *hooks);

/* ---- System files ---------------------------------------------------------- */

/*
 * For system code, not users: reads a FAT path such as "/UOS/SYSUAF.DAT"
 * with no access check, recovering an interrupted atomic write first.
 * Same buffer rules and limit as vfs_read_all(). The path must start with
 * "/UOS/". Loop.
 */
int vfs_sys_read_all(const char *fat_path, size_t max_bytes, char **buf, size_t *len);

/* The session service's write of the user database, as vfs_write_all() but
 * with no access check. The path must start with "/UOS/". Loop. */
int vfs_sys_write_all(const char *fat_path, const void *data, size_t len);

#endif
