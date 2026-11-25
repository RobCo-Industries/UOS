/*
 * The file system service's decisions (pure): error mapping, dates, work
 * file names, wildcard targets and the listing filter. No hardware, no
 * globals, no allocation. vfs.c calls these; tests/host/test_vfs_rules.c
 * tests them.
 */
#ifndef UOS_VFS_RULES_H
#define UOS_VFS_RULES_H

#include "vfs.h"

/* A library FS_* code (0 or negative) as a VFS_* code. `writing` picks
 * VFS_E_WRITE over VFS_E_READ for device errors. */
int vfs_from_fs(int fs_err, int writing);

/* A vp_err_t as a VFS_* code: access codes keep their meaning, the rest
 * are VFS_E_SPEC. */
int vfs_from_vp(int vp_err);

/* "DD-MMM-YYYY HH:MM:SS". A field out of range gives the same shape with
 * every character a question mark. 0, or VFS_E_ARG when size is below
 * VFS_DATE_LEN + 1. */
int vfs_fmt_datetime(uint16_t date, uint16_t time, char *buf, size_t size);

/* The work name of `path`: `mark` ('~' or '#') before the last component.
 * "/UOS/A.TXT" with '~' is "/UOS/~A.TXT". 0 or VFS_E_ARG. */
int vfs_work_name(const char *path, char mark, char *out, size_t size);

/*
 * Which name holds a file's content after an interrupted atomic write,
 * by the library's recovery table, from which of the three names exist:
 * 0 for the file itself (present, or nothing to find), '~' when "~NAME"
 * and "#NAME" both exist, '#' when only "#NAME" does. "~NAME" alone holds
 * nothing: recovery removes it.
 */
int vfs_atomic_pick(int target, int work_new, int work_old);

/*
 * The target name of a copy or rename. `tname` and `ttype` are the
 * resolved target's parts; an empty name with `tdir` set means a folder
 * only, which keeps the source's name and type. A part that is exactly "*"
 * takes the source's part. Any other wildcard is VFS_E_SPEC.
 */
int vfs_wild_target(const char *sname, const char *stype, const char *tname,
                    const char *ttype, int tdir, char *oname, char *otype);

/* The folder that holds a file result: one level up in path and parts.
 * A directory result is copied as it is. 0 or VFS_E_SPEC. */
int vfs_parent_res(const vp_res_t *r, vp_res_t *dir);

/*
 * The result for one entry of the folder `dir` (a directory result):
 * name `fatname` as the library gives it, a folder when is_dir. 0, or
 * VFS_E_SPEC when the name is not 8.3 or the folder is too deep.
 */
int vfs_entry_res(const vp_res_t *dir, const char *fatname, int is_dir, vp_res_t *out);

/* Results of vfs_list_keep(). */
#define VFS_KEEP_NO       0   /* not a match: work file, not 8.3, other name */
#define VFS_KEEP_YES      1   /* a match the caller may use */
#define VFS_KEEP_REFUSED  2   /* a match the access table refused: count it */

/*
 * The listing filter for one directory entry: VFS_KEEP_YES to show it
 * with *out filled, VFS_KEEP_NO to hide it as no match, VFS_KEEP_REFUSED
 * to hide it as a refused match. No match: library work files
 * (`leftover`), names that are not 8.3, names that do not match the
 * pattern. Refused: a match the access table does not let `privs` list
 * (op VP_OP_LIST) or use for `op`. Callers count refused matches, so a
 * wildcard whose every match is refused reports no privilege, not no files.
 */
int vfs_list_keep(const vp_res_t *dir, const char *fatname, int is_dir, int leftover,
                  const char *pname, const char *ptype, const char *home, uint32_t privs,
                  int op, vp_res_t *out);

/*
 * Adds one entry to a sorted array of at most `max`, keeping the first
 * `max` entries in vp_entry_cmp() order: a larger entry than all `max`
 * kept ones is dropped. *count grows up to max.
 */
void vfs_list_insert(vfs_entry_t *e, size_t *count, size_t max, const vfs_entry_t *n);

#endif
