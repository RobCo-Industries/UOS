# vfs path: file specifications

`src/vfs/path.c` turns OpenVMS-style file specifications into FAT paths and back. It is pure: no hardware, no file access, no globals, no allocation. Public names start with `vp_`. Nothing here is validated on real hardware.

## Grammar

```
spec      = [device ":"] [directory] [name] ["." [type]] [";" [version]]
device    = 1 to 15 of A-Z 0-9 $ _
directory = "[" "]"                      current directory
          | "[000000]"                   device root
          | "[" comp {"." comp} "]"      absolute from the device root
          | "[." comp {"." comp} "]"     relative to the current directory
          | "[" "-" {".-"} {"." comp} "]" parent markers, then components
name      = 0 to 8 of A-Z 0-9 $ _ - and, where allowed, * %
type      = 0 to 3 of the same
comp      = 1 to 8 of A-Z 0-9 $ _ -
version   = 0 to 5 digits, value 0 to 32767
```

- Input is case-insensitive. Every part is stored upper-case.
- A version is accepted and ignored. Displays show `;1`.
- `-` is a parent marker only as a whole leading component. `[-A]` names a folder `-A`. `[A.-]` and `[.-]` are errors.
- Wildcards `*` and `%` work in the name and the type only. They count toward the 8 and 3 character limits.
- No space, `/`, `\`, `:` after the device, `<`, `>` or control character is valid anywhere.

## Devices and logical names

| Name | Tree | Root |
|---|---|---|
| `SD0:` | UOS | `/UOS` |
| `BOOT:` | BOOT | `/ROBCO`, read-only |
| `DEV:` | DEV | the device table |
| `SYS$SYSTEM:` | UOS | `/UOS/SYS` |
| `SYS$HELP:` | UOS | `/UOS/SYS/HELP` |
| `SYS$HOLO:` | UOS | `/UOS/HOLO` |
| `SYS$LOGIN:` | UOS | `/UOS/HOME/<home>` |

Any other device name is `VP_E_NODEV`.

## Resolution rules

`vp_resolve(ctx, spec, deftype, flags, out)` fills a `vp_res_t`: the tree, the directory components below the tree root, the name, the type and the FAT path.

- No device: the context's tree. `[A.B]` starts at its root. `[]`, `[.A]`, `[-]` and a bare name start at the current directory.
- `SD0:` or `BOOT:`: relative forms start at the current directory when the context is on that tree, otherwise at the tree root.
- Logical names are rooted. `[A]` and `[.A]` both mean a folder under the logical root. `[-]` cannot climb above the logical root: `SYS$LOGIN:[-]` is `VP_E_ROOT`.
- `[-]` above the device root is `VP_E_ROOT`. More than 8 levels is `VP_E_DEPTH`.
- `DEV:` takes a device name only. A directory, a dot or a wildcard is `VP_E_DEVDIR`. The path is the device name. `DEV:` alone is the device table, a directory-only result.
- No name and no dot: a directory-only result, `is_dir` set, path is the folder. With `VP_R_NEED_NAME` it is `VP_E_NONAME`.
- A dot or a version with no name is `VP_E_NONAME`.
- Default type rule: when the name has no dot, `deftype` (if not NULL) becomes the type. `README` with `TXT` is `README.TXT`. `README.` keeps an empty type. An explicit type always wins.
- With `VP_R_WILD_OK` a wildcard name or type sets `wild`; the path is then the folder to list and `name`, `type` hold the pattern.
- With `VP_R_EMPTY_OK` an empty specification is the current directory. Without it, it is `VP_E_EMPTY`.
- `VP_PATH_MAX` is 96. The longest possible path is 91 characters, so it always fits.

Paths: `SD0:[HOME.ADMIN]A.TXT` is `/UOS/HOME/ADMIN/A.TXT`. A name with no type has no dot: `/UOS/HOME/ADMIN/README`.

## Context

`vp_ctx_t` holds the current tree and directory and the user's home folder. The caller owns it.

- `vp_ctx_init(ctx, home)`: default `SD0:[HOME.<home>]`. A NULL, empty or invalid home gives no home and the default `SD0:[000000]`.
- `vp_set_default(ctx, spec)`: resolves like `vp_resolve` with no flags. The result must be a directory in UOS or BOOT, else `VP_E_NOTDIR`. On error the context does not change.
- `vp_show_default(ctx, buf, size)`: `SD0:[HOME.ADMIN]`, or `SD0:[000000]` at the root.

## Formatting

`vp_format(tree, path, is_dir, buf, size)` turns a FAT path back into a specification for display:

- `/UOS/HOME/ADMIN/NOTES.TXT` gives `SD0:[HOME.ADMIN]NOTES.TXT;1`.
- `/UOS/UOS.CFG` gives `SD0:[000000]UOS.CFG;1`.
- `/UOS/README` gives `SD0:[000000]README.;1`. The dot keeps a round trip from taking a default type.
- A directory `/UOS/HOME/ADMIN/MAIL` gives `SD0:[HOME.ADMIN]MAIL.DIR;1`. `/UOS` gives `SD0:[000000]000000.DIR;1`.
- `DOOR1` in DEV gives `DEV:DOOR1;1`.

A path outside the tree root, an empty component, a component that is not 8.3, or more than 8 folders is `VP_E_BADPATH`. `VP_DISPLAY_MAX` (128) always fits.

Every function that fills a buffer returns `VP_E_BUF` when the result plus its terminator does not fit, and leaves an empty string.

## Wildcards, listings and sorting

- `vp_match_part(pattern, s)`: `*` any run, `%` one character, case-insensitive.
- `vp_match(pname, ptype, name, type)`: both parts must match. A directory matches with type `DIR`.
- `vp_list_pattern(spec, &pname, &ptype)`: for listings. No name gives `*`. No dot gives type `*`. So an empty specification or `[.MAIL]` lists `*.*`, and `A` lists `A.*`.
- `vp_entry_from_fat(fatname, is_dir, out)`: splits a FAT 8.3 name. A directory gets type `DIR`.
- `vp_entry_cmp` and `vp_sort_entries`: order by name, then type. The sort is an insertion sort in place.

## Access rules

`vp_access(res, home, privs, op)` returns `VP_OK`, `VP_E_NOPRIV`, `VP_E_READONLY` or `VP_E_PROTECTED`. `home` is the upper-case home folder. Privilege bits follow section 5 of the design: 0 `SYSTEM`, 1 `OPER`, 2 `SECURITY`, 3 `READALL`. `OPER` changes nothing here. Write covers create and rename.

Each cell is read, write, delete, list. OK is allowed, NP is `VP_E_NOPRIV`, RO is `VP_E_READONLY`, PR is `VP_E_PROTECTED`.

| Area | No privilege, OPER | SECURITY | READALL | SYSTEM |
|---|---|---|---|---|
| `SYS$SYSTEM`, `SYS$HELP`, `SYS$HOLO` and their folders | OK NP NP OK | OK NP NP OK | OK NP NP OK | OK OK OK OK |
| Own home: files and subfolders | OK OK OK OK | OK OK OK OK | OK OK OK OK | OK OK OK OK |
| Own home folder itself | OK NP NP OK | OK NP NP OK | OK NP NP OK | OK OK OK OK |
| Another home, or a file directly in `HOME` | NP NP NP NP | NP NP NP NP | OK NP NP OK | OK OK OK OK |
| The `HOME` folder | NP NP NP NP | NP NP NP NP | NP NP NP OK | OK OK OK OK |
| `/UOS/SYSUAF.DAT` | NP PR PR NP | OK PR PR OK | NP PR PR NP | OK PR PR OK |
| The root, other root files, other folders such as `WORDS` | NP NP NP NP | NP NP NP NP | NP NP NP NP | OK OK OK OK |
| `BOOT:` | NP RO RO NP | NP RO RO NP | NP RO RO NP | OK RO RO OK |
| `DEV:` | OK OK NP OK | OK OK NP OK | OK OK NP OK | OK OK NP OK |

- `DEV:` reads and writes pass here. The device module checks each device's own `privs` mask.
- A wildcard result checks the folder. The caller checks each matched file again, so `DELETE *.*` at the root still meets the `SYSUAF.DAT` rule.
- An unknown op is `VP_E_NOPRIV`.

## Limits

| Constant | Value |
|---|---|
| `VP_NAME_MAX` | 8 |
| `VP_TYPE_MAX` | 3 |
| `VP_DEPTH_MAX` | 8 |
| `VP_DEV_MAX` | 15 |
| `VP_SPEC_MAX` | 255 |
| `VP_VER_MAX` | 32767 |
| `VP_PATH_MAX` | 96 |
| `VP_DISPLAY_MAX` | 128 |

## Errors

`vp_strerror(code)` gives one line for each code: `VP_E_EMPTY`, `VP_E_TOOLONG`, `VP_E_BADCHAR`, `VP_E_NAMELEN`, `VP_E_TYPELEN`, `VP_E_DEPTH`, `VP_E_BRACKET`, `VP_E_BADVER`, `VP_E_WILDDIR`, `VP_E_WILD`, `VP_E_BADDIR`, `VP_E_DEVLEN`, `VP_E_NODEV`, `VP_E_ROOT`, `VP_E_NONAME`, `VP_E_NOTDIR`, `VP_E_DEVDIR`, `VP_E_NOHOME`, `VP_E_BUF`, `VP_E_BADPATH`, `VP_E_NOPRIV`, `VP_E_READONLY`, `VP_E_PROTECTED`. `[...]` is the OpenVMS folder wildcard, so it is `VP_E_WILDDIR`. The parser reports the first error from left to right.

## Tests

`make test-host` runs `tests/host/test_vfs_path.c`. See [TESTING.md](../TESTING.md).
