# DCL: the command language

`src/dcl/` holds the UOS command language. It is pure code: no hardware, no files, no allocation, no globals. The caller owns every buffer. The public header is `src/dcl/dcl.h`, and every public name starts with `dcl_`.

Nothing here is validated on real hardware. The host tests run on a computer, and the sources are compiled for the ARM target to check warnings only.

## Files

- `dcl.c`: the tokeniser, line continuation, numbers, lookup, the table check and the parser.
- `msg.c`: the message catalogue and `dcl_format_message()`.
- `tables.c`: the UOS command table, `dcl_uos_table`. Data only.

## Grammar

```
line       = [ verb { item } ] [ comment ]
item       = parameter | qualifier
qualifier  = "/" name [ "=" value ]
value      = word | "(" word { "," word } ")"
word       = { wordchar | quoted }+
quoted     = '"' { any printable character, '""' for '"' } '"'
comment    = "!" { any character }
```

- Blanks are spaces and tabs. They separate words. Blanks around `=` and inside a list are allowed.
- A word character is any printable ASCII character except blank, `/`, `=`, `(`, `)`, `,`, `!` and `"`. File specifications such as `SD0:[-]X.TXT;1` are single words.
- Outside quotes, letters become upper case. Inside quotes, case and blanks stay. Quoted parts and plain parts next to each other join into one word: `X"y z"W` is `Xy zW`.
- `!` outside quotes starts a comment that runs to the end of the line.
- Parameters and qualifiers come in any order after the verb. For a verb with a second level, the first parameter is the keyword.
- `=`, `(`, `)` and `,` outside a qualifier value are errors (`IVCHAR`).
- A qualifier given twice keeps the last occurrence.
- Control characters and bytes above 126 are errors, inside quotes too. A tab inside quotes is kept.

### Continuation

`dcl_line_add()` joins typed lines. When the last non-blank character outside a comment is `-` and is not inside quotes, the function drops the `-` and returns `DCL_CONTIN`. The caller reads another line and calls it again. Text is joined with no added blank, so `DIR-` and `ECTORY` give `DIRECTORY`. When the joined line would pass 255 characters, it returns `DCL_LINOVF` and empties the buffer.

### Numbers

`dcl_parse_number()` reads an optional sign, then an optional radix prefix, then digits: `%D` decimal, `%X` hexadecimal, `%O` octal, decimal without a prefix. Values are 32-bit signed. A malformed number is `IVNUMB`. A number outside the int32 range or outside the given minimum and maximum is `NUMRNG`.

## Abbreviation rule

`dcl_lookup()` applies the same rule to verbs, keywords, keyword values and qualifiers:

1. A word equal to a name matches that name, even when it also prefixes a longer name.
2. Otherwise the word must be a prefix of exactly one name. No match is "unrecognized". Two or more matches is "ambiguous". These are different errors: `IVVERB` and `ABVERB`, `IVKEYW` and `ABKEYW`, `IVQUAL` and `ABQUAL`.
3. Any prefix of 4 or more characters of a name unique in its first four characters is found. `dcl_table_check()` refuses a table where two verbs, two keywords of one verb, two values of one set, or two qualifiers of one set share their first four characters, so in a sound table 4 characters always suffice.

Lookup compares upper case. The tokeniser upper-cases words, so the user can type any case.

For qualifiers, `NO` negation works this way. The whole word is looked up first, so a qualifier really named `NOTE` wins. If that fails and the word starts with `NO`, the rest is looked up. A match on a negatable qualifier is negated. A match on one that is not negatable is `NONEG`. Otherwise the error of the whole word is reported.

Qualifiers are looked up in the keyword's set first, then in the verb's set.

## Table structure

```c
dcl_table_t  { count, verbs }
dcl_verb_t   { name, nreq, nmax, params, nquals, quals, nkeys, keys, key_prompt, privs, help }
dcl_param_t  { prompt, type, keys, min, max }
dcl_qual_t   { name, flags, value, type, keys, min, max, def }
```

- A keyword of the second level is a `dcl_verb_t` too, with its own parameters, qualifiers and privileges. A verb with keywords has `nmax = 0` and a `key_prompt`. Keywords have no keywords and no help key.
- Parameter and value types: `DCL_VT_FILE` (checked later by the path code), `DCL_VT_KEYWORD` (one of a NULL-terminated set; the full keyword is stored), `DCL_VT_NUMBER` (between `min` and `max`; the value is also stored as a number), `DCL_VT_TEXT`.
- Qualifier flags: `DCL_QF_NEG` allows `/NOname`. `DCL_QF_LIST` allows a parenthesised list. `DCL_QF_DEFON` makes the qualifier count as present when not given.
- Qualifier value: `DCL_QV_NONE`, `DCL_QV_OPTIONAL` (then `def` is the value used when it is left out), `DCL_QV_REQUIRED`. A negated qualifier takes no value.
- `privs` is a mask of `DCL_PRIV_SYSTEM`, `DCL_PRIV_OPER`, `DCL_PRIV_SECURITY`, `DCL_PRIV_READALL`, the bits of `SYSUAF.DAT`. A keyword's mask adds to its verb's. `SYSTEM` grants every privilege.
- `help` names the help file, `SYS$HELP:<VERB>.HLP`. FAT names have at most 8 characters, so `DIRECTORY` uses `DIRECTOR.HLP` and `AUTHORIZE` uses `AUTHORIZ.HLP`.

## Public functions

| Function | What it does |
|---|---|
| `dcl_lex_init`, `dcl_lex_next` | Tokenise a line into words and punctuation. Processed text goes to a caller buffer. |
| `dcl_line_reset`, `dcl_line_add` | Join continued lines. |
| `dcl_parse_number` | Read a number with a range check. |
| `dcl_lookup`, `dcl_lookup_key` | Find a name by the abbreviation rule. |
| `dcl_table_check` | Check a table is sound; names the first bad entry. |
| `dcl_parse` | Parse one line against a table into a `dcl_cmd_t`, or return one error. |
| `dcl_cmd_qual` | The qualifier as given, by full name. |
| `dcl_cmd_on` | Whether a qualifier is in effect, default included. |
| `dcl_cmd_value` | A qualifier's first value, or its default. |
| `dcl_cmd_privs`, `dcl_check_privs` | The privileges a command needs, and the check against a user's mask. |
| `dcl_msg_facility`, `dcl_msg_severity`, `dcl_msg_ident`, `dcl_msg_text` | Parts of a message. |
| `dcl_format_message` | Format a message into a bounded buffer. |

### The parse result

`dcl_parse()` returns `DCL_NORMAL` or one error. On success `cmd->verb` is set, or NULL for an empty or comment-only line. `cmd->keyword` is the second level. `params[]` hold the parameters as strings, `numbers[]` their values for number parameters. `quals[]` hold the qualifiers given, each with `def`, `typed`, `negated`, `values[]` and `numbers[]`. All strings point into `cmd->text`.

On error `cmd->token` names the token that caused it, or is NULL for errors with no single token. Errors are checked in this order: tokens and structure in line order, then the keyword, then qualifiers, then too many parameters, then parameter values, then missing parameters. So `INSFPRM` comes only for a line that is otherwise valid. With `INSFPRM`, `cmd->prompt` is the prompt of the first missing parameter or of the missing keyword. The caller shows it, reads the answer, appends it to the line and parses again.

`dcl_cmd_t` is 1816 bytes on the target. Keep it in a context struct or static storage, not on the stack.

## Limits

| Constant | Value | Meaning |
|---|---|---|
| `DCL_LINE_MAX` | 255 | characters in a line, continuations joined |
| `DCL_TOKEN_MAX` | 127 | characters in one word after processing |
| `DCL_PARAM_MAX` | 8 | parameters on a line, and per verb or keyword |
| `DCL_QUAL_MAX` | 16 | qualifiers on a line, and per set in a table |
| `DCL_LIST_MAX` | 8 | values in one list |
| `DCL_ABBREV_SURE` | 4 | characters that always suffice |
| `DCL_TEXT_SIZE` | 512 | processed token storage in `dcl_cmd_t` |
| `DCL_MSG_SIZE` | 256 | a buffer that holds any message with a 127-character token |

## Messages

Shape: `%FACILITY-S-IDENT, text`. Severity is `S`, `I`, `W`, `E` or `F`. In the text, `!S` takes the string argument, `!N` the number, and `!P` gives `s` unless the number is 1. When the text has no `!S` and a string is passed, a second line ` \TOKEN\` names it. `dcl_format_message()` returns the length, or -1 with an empty buffer when the buffer is too small.

| Code | Message |
|---|---|
| `DCL_NORMAL` | `%DCL-S-NORMAL, normal successful completion` |
| `DCL_CONTIN` | `%DCL-I-CONTIN, command continues on the next line` |
| `DCL_IVVERB` | `%DCL-W-IVVERB, unrecognized command verb - check validity and spelling` |
| `DCL_ABVERB` | `%DCL-W-ABVERB, ambiguous command verb - supply more characters` |
| `DCL_IVKEYW` | `%DCL-W-IVKEYW, unrecognized keyword - check validity and spelling` |
| `DCL_ABKEYW` | `%DCL-W-ABKEYW, ambiguous keyword - supply more characters` |
| `DCL_IVQUAL` | `%DCL-W-IVQUAL, unrecognized qualifier` |
| `DCL_ABQUAL` | `%DCL-W-ABQUAL, ambiguous qualifier - supply more characters` |
| `DCL_NONEG` | `%DCL-W-NONEG, qualifier cannot be negated` |
| `DCL_INSFPRM` | `%DCL-W-INSFPRM, missing command parameters - supply all required parameters` |
| `DCL_MAXPARM` | `%DCL-W-MAXPARM, too many parameters` |
| `DCL_MAXQUAL` | `%DCL-W-MAXQUAL, too many qualifiers` |
| `DCL_VALREQ` | `%DCL-W-VALREQ, missing qualifier or keyword value` |
| `DCL_NOVALU` | `%DCL-W-NOVALU, value not allowed` |
| `DCL_NOLIST` | `%DCL-W-NOLIST, list of values not allowed` |
| `DCL_MAXVAL` | `%DCL-W-MAXVAL, too many values in list` |
| `DCL_IVNUMB` | `%DCL-W-IVNUMB, invalid number` |
| `DCL_NUMRNG` | `%DCL-W-NUMRNG, number out of range` |
| `DCL_UNTERM` | `%DCL-W-UNTERM, unterminated quoted string` |
| `DCL_PARENS` | `%DCL-W-PARENS, missing closing parenthesis` |
| `DCL_IVCHAR` | `%DCL-W-IVCHAR, invalid character in command` |
| `DCL_LINOVF` | `%DCL-W-LINOVF, command line too long` |
| `DCL_TKNOVF` | `%DCL-W-TKNOVF, command element too long` |
| `DCL_SYS_NOPRIV` | `%UOS-E-NOPRIV, not enough privilege for this operation` |
| `DCL_SYS_FNF` | `%UOS-E-FNF, file not found` |
| `DCL_SYS_DNF` | `%UOS-E-DNF, directory not found` |
| `DCL_SYS_NOFILES` | `%UOS-W-NOFILES, no files found` |
| `DCL_SYS_EXISTS` | `%UOS-E-EXISTS, file already exists` |
| `DCL_SYS_IVSPEC` | `%UOS-E-IVSPEC, invalid file specification` |
| `DCL_SYS_READERR` | `%UOS-E-READERR, error reading file` |
| `DCL_SYS_WRITERR` | `%UOS-E-WRITERR, error writing file` |
| `DCL_SYS_DISKFULL` | `%UOS-E-DISKFULL, no space left on the volume` |
| `DCL_SYS_NODEV` | `%UOS-E-NODEV, no such device` |
| `DCL_SYS_NOUSER` | `%UOS-E-NOUSER, no such user` |
| `DCL_SYS_CANCEL` | `%UOS-I-CANCEL, operation cancelled` |
| `DCL_SYS_COPIED` | `%COPY-S-COPIED, !N file!P copied` |
| `DCL_SYS_DELETED` | `%DELETE-S-DELETED, !N file!P deleted` |
| `DCL_SYS_RENAMED` | `%RENAME-S-RENAMED, !N file!P renamed` |
| `DCL_SYS_CREATED` | `%CREATE-S-CREATED, !S created` |
| `DCL_SYS_WRITLCK` | `%UOS-E-WRITLCK, volume is read-only` |
| `DCL_SYS_PROTECT` | `%UOS-E-PROTECT, file is protected` |
| `DCL_SYS_TOOBIG` | `%UOS-E-TOOBIG, file is too large` |
| `DCL_SYS_INSFMEM` | `%UOS-E-INSFMEM, not enough memory for this operation` |

## The UOS command table

`P` lists parameters as prompt and type, with the required count first. Qualifiers marked `neg` take `/NO`. No qualifier in this table is on by default.

| Verb | Shortest | P | Qualifiers | Privilege |
|---|---|---|---|---|
| `DIRECTORY` | `DI` | 0 required: File (file) | `/FULL` neg, `/SIZE` neg, `/DATE` neg, `/OUTPUT=file` | |
| `TYPE` | `T` | 1: File (file) | `/PAGE` neg | |
| `COPY` | `CO` | 2: From, To (file) | `/LOG` neg, `/CONFIRM` neg | |
| `DELETE` | `DEL` | 1: File (file) | `/LOG` neg, `/CONFIRM` neg | |
| `RENAME` | `RE` | 2: From, To (file) | `/LOG` neg, `/CONFIRM` neg | |
| `CREATE` | `CR` | 1: File (file) | `/DIRECTORY`, `/LOG` neg | |
| `SET` | `SE` | keyword, prompt What | see below | |
| `SHOW` | `SH` | keyword, prompt What | `/OUTPUT=file` | |
| `HELP` | `HE` | 0 required: Topic (text) | | |
| `RUN` | `RU` | 1: Program, `HACK`, `TERMINAL` or `HOLOTAPE` | | |
| `LOGIN` | `LOGI` | 0 required: Username (text) | | |
| `LOGOUT` | `LOGO` | none | | |
| `ROBCO` | `RO` | none | | |
| `DEVICE` | `DEV` | 1 required of 2: Device, Action (text) | | |
| `HOLOTAPE` | `HO` | 0 required of 2: Tape, Entry (text) | | |
| `MAIL` | `M` | keyword, prompt Action | see below | |
| `AUTHORIZE` | `A` | keyword, prompt Action | see below | `SECURITY` |

The "Shortest" column is informative. The test finds every verb's shortest prefix by trying lengths, so the column is not used by any test.

`SET` keywords:

| Keyword | P | Qualifiers | Privilege |
|---|---|---|---|
| `DEFAULT` | 1: Directory (file) | | |
| `PASSWORD` | none | | |
| `TERMINAL` | none | `/SCHEME=` one of `GREEN`, `AMBER`, `BLUE`, `WHITE` | |
| `HALT` | 0 required: Action, `RESTART` | `/MAINTENANCE` neg | `OPER` |

`SET HALT` halts. `SET HALT RESTART` restarts. `/MAINTENANCE` with `RESTART` asks the boot agent for the maintenance image.

`SHOW` keywords: `DEFAULT`, `USERS`, `SYSTEM`, `TIME`, `DEVICES` with an optional Device (text), `TERMINAL`, `MEMORY`. None takes a parameter except `DEVICES`. `SHOW DEVICE DOOR1` works through abbreviation.

`MAIL` keywords:

| Keyword | P | Qualifiers |
|---|---|---|
| `DIRECTORY` | none | |
| `READ` | 0 required: Message (number 1 to 9999) | |
| `SEND` | 1: To (text) | `/SUBJECT=text` |
| `DELETE` | 1: Message (number 1 to 9999) | |

`AUTHORIZE` keywords, all needing `SECURITY` through the verb:

| Keyword | P | Qualifiers |
|---|---|---|
| `ADD` | 1: Username (text) | user qualifiers |
| `MODIFY` | 1: Username (text) | user qualifiers |
| `REMOVE` | 1: Username (text) | |
| `LIST` | 0 required: Username (text) | |

User qualifiers: `/PASSWORD=text`, `/PRIVS=` one value or a list from `SYSTEM`, `OPER`, `SECURITY`, `READALL` (neg: `/NOPRIVS` clears them), `/HOME=text`, `/DISABLED` neg (flag bit 0), `/HACKABLE` neg (`/NOHACKABLE` sets flag bit 1).

## Tests

`make test-host` runs `test_dcl`, `test_dcl_msg` and `test_dcl_table`. See [TESTING.md](../TESTING.md).
