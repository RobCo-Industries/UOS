/*
 * DCL: the UOS command language. Pure code: a tokeniser, a command table
 * held as data, lookup with abbreviations, a parser that fills a dcl_cmd_t
 * or reports one error, numbers, and the message catalogue.
 */
#ifndef UOS_DCL_H
#define UOS_DCL_H

#include <stddef.h>
#include <stdint.h>

/* Limits. */
#define DCL_LINE_MAX     255  /* characters in a command line, continuations joined */
#define DCL_TOKEN_MAX    127  /* characters in one word or quoted string, after processing */
#define DCL_PARAM_MAX    8    /* parameters on a line, and per verb or keyword in a table */
#define DCL_QUAL_MAX     16   /* qualifiers on a line */
#define DCL_LIST_MAX     8    /* values in one parenthesised list */
#define DCL_ABBREV_SURE  4    /* a prefix this long always matches a name unique in its first four */
#define DCL_TEXT_SIZE    (2 * (DCL_LINE_MAX + 1)) /* processed token storage in dcl_cmd_t */
#define DCL_MSG_SIZE     256  /* a buffer this size holds any formatted message with a token */

/* Privilege bits, as in SYSUAF.DAT. SYSTEM grants every other one. */
#define DCL_PRIV_SYSTEM   (1u << 0)
#define DCL_PRIV_OPER     (1u << 1)
#define DCL_PRIV_SECURITY (1u << 2)
#define DCL_PRIV_READALL  (1u << 3)

/* The message catalogue. Every status in this module is one of these. */
typedef enum {
    DCL_NORMAL = 0, /* success */
    DCL_CONTIN,     /* the line ends with '-': supply another line */
    /* Parser and tokeniser errors. */
    DCL_IVVERB,
    DCL_ABVERB,
    DCL_IVKEYW,
    DCL_ABKEYW,
    DCL_IVQUAL,
    DCL_ABQUAL,
    DCL_NONEG,
    DCL_INSFPRM,
    DCL_MAXPARM,
    DCL_MAXQUAL,
    DCL_VALREQ,
    DCL_NOVALU,
    DCL_NOLIST,
    DCL_MAXVAL,
    DCL_IVNUMB,
    DCL_NUMRNG,
    DCL_UNTERM,
    DCL_PARENS,
    DCL_IVCHAR,
    DCL_LINOVF,
    DCL_TKNOVF,
    /* General system messages the commands share. */
    DCL_SYS_NOPRIV,
    DCL_SYS_FNF,
    DCL_SYS_DNF,
    DCL_SYS_NOFILES,
    DCL_SYS_EXISTS,
    DCL_SYS_IVSPEC,
    DCL_SYS_READERR,
    DCL_SYS_WRITERR,
    DCL_SYS_DISKFULL,
    DCL_SYS_NODEV,
    DCL_SYS_NOUSER,
    DCL_SYS_CANCEL,
    DCL_SYS_COPIED,
    DCL_SYS_DELETED,
    DCL_SYS_RENAMED,
    DCL_SYS_CREATED,
    DCL_MSG_COUNT
} dcl_msg_t;

/* Severity letters: 'S' success, 'I' information, 'W' warning, 'E' error, 'F' fatal. */
const char *dcl_msg_facility(dcl_msg_t id);
char dcl_msg_severity(dcl_msg_t id);
const char *dcl_msg_ident(dcl_msg_t id);
/* The text template. "!S" takes the string argument, "!N" the number, "!P" is "s" unless the number is 1. */
const char *dcl_msg_text(dcl_msg_t id);

/*
 * Formats "%FAC-S-IDENT, text" into buf. Placeholders take arg and num.
 * If the text has no "!S" and arg is not NULL, a second line " \ARG\" names
 * the token. Returns the length, or -1 when buf is too small or id is not a
 * message; buf then holds "" if size > 0.
 */
int dcl_format_message(char *buf, size_t size, dcl_msg_t id, const char *arg, uint32_t num);

/* Tokens. */
typedef enum {
    DCL_TOK_END = 0,
    DCL_TOK_WORD,   /* a word, quoted parts included; text is processed */
    DCL_TOK_SLASH,
    DCL_TOK_EQUALS,
    DCL_TOK_LPAREN,
    DCL_TOK_RPAREN,
    DCL_TOK_COMMA
} dcl_tok_type_t;

typedef struct {
    dcl_tok_type_t type;
    uint8_t quoted;     /* the word had at least one quoted part */
    const char *text;   /* WORD only: NUL-terminated inside the lexer's out buffer */
} dcl_token_t;

typedef struct {
    const char *p;
    char *out;
    size_t out_size;
    size_t out_len;
} dcl_lexer_t;

void dcl_lex_init(dcl_lexer_t *lx, const char *line, char *out, size_t out_size);
/* DCL_NORMAL with a token, or DCL_UNTERM, DCL_TKNOVF, DCL_IVCHAR, DCL_LINOVF (out full). */
dcl_msg_t dcl_lex_next(dcl_lexer_t *lx, dcl_token_t *tok);

/* Continuation: joins lines that end with '-' outside quotes and comments. */
typedef struct {
    char text[DCL_LINE_MAX + 1];
    size_t len;
} dcl_line_t;

void dcl_line_reset(dcl_line_t *ln);
/*
 * Appends one typed line. DCL_NORMAL: ln->text holds the whole command.
 * DCL_CONTIN: the '-' is dropped; call again with the next line.
 * DCL_LINOVF: the joined line would pass DCL_LINE_MAX; ln is reset.
 */
dcl_msg_t dcl_line_add(dcl_line_t *ln, const char *piece);

/* Numbers: decimal, %D decimal, %X hexadecimal, %O octal, optional sign first. */
dcl_msg_t dcl_parse_number(const char *s, int32_t min, int32_t max, int32_t *out);

/* The command table. */
typedef enum {
    DCL_VT_NONE = 0,
    DCL_VT_FILE,     /* a file specification, checked later by the caller */
    DCL_VT_KEYWORD,  /* one of keys, abbreviations allowed; the full keyword is stored */
    DCL_VT_NUMBER,   /* min..max */
    DCL_VT_TEXT      /* any word or quoted string */
} dcl_vtype_t;

typedef struct {
    const char *prompt;        /* shown by the caller as "_<prompt>: " */
    dcl_vtype_t type;
    const char *const *keys;   /* DCL_VT_KEYWORD: NULL-terminated */
    int32_t min, max;          /* DCL_VT_NUMBER */
} dcl_param_t;

#define DCL_QF_NEG    0x01  /* /NOname is allowed */
#define DCL_QF_LIST   0x02  /* the value may be a parenthesised list */
#define DCL_QF_DEFON  0x04  /* counts as present when not given */

typedef enum {
    DCL_QV_NONE = 0,
    DCL_QV_OPTIONAL,
    DCL_QV_REQUIRED
} dcl_qvalue_t;

typedef struct {
    const char *name;          /* first member: lookup relies on it */
    uint8_t flags;
    dcl_qvalue_t value;
    dcl_vtype_t type;          /* of the value */
    const char *const *keys;
    int32_t min, max;
    const char *def;           /* value used when the value is optional and left out */
} dcl_qual_t;

/* A verb, or a keyword of a verb's second level: the same shape. */
typedef struct dcl_verb {
    const char *name;          /* first member: lookup relies on it */
    uint8_t nreq;              /* required parameters */
    uint8_t nmax;              /* parameters accepted, at most DCL_PARAM_MAX */
    const dcl_param_t *params;
    uint8_t nquals;
    const dcl_qual_t *quals;
    uint8_t nkeys;             /* second level; 0 for none */
    const struct dcl_verb *keys;
    const char *key_prompt;    /* asked for when the keyword is missing */
    uint32_t privs;            /* needed; a keyword's add to its verb's */
    const char *help;          /* "SYS$HELP:<VERB>.HLP"; NULL on keywords */
} dcl_verb_t;

typedef struct {
    size_t count;
    const dcl_verb_t *verbs;
} dcl_table_t;

/* The UOS base table, from tables.c. */
extern const dcl_table_t dcl_uos_table;

/* Lookup. */
typedef enum {
    DCL_LK_FOUND = 0,
    DCL_LK_NONE,
    DCL_LK_AMBIG
} dcl_lookup_t;

/*
 * Finds word among count items, stride bytes apart, each starting with a
 * "const char *" name. An exact match wins; otherwise the word must be a
 * prefix of exactly one name. Comparison is case-sensitive: callers pass
 * upper case.
 */
dcl_lookup_t dcl_lookup(const char *word, const void *items, size_t count, size_t stride,
                        size_t *index);
/* The same over a NULL-terminated array of names. */
dcl_lookup_t dcl_lookup_key(const char *word, const char *const *keys, size_t *index);

/*
 * Checks a table: names upper case, verbs and keywords unique in their first
 * DCL_ABBREV_SURE characters, nreq <= nmax <= DCL_PARAM_MAX, qualifier
 * counts at most DCL_QUAL_MAX. Returns 0 when sound, or -1 with *bad set to
 * the first offending name.
 */
int dcl_table_check(const dcl_table_t *table, const char **bad);

/* The parse. */
typedef struct {
    const dcl_qual_t *def;
    const char *typed;              /* the name as typed, NO included */
    uint8_t negated;
    uint8_t nvalues;
    const char *values[DCL_LIST_MAX];
    int32_t numbers[DCL_LIST_MAX];  /* DCL_VT_NUMBER values */
} dcl_qval_t;

typedef struct {
    const dcl_verb_t *verb;     /* NULL for an empty or comment-only line */
    const dcl_verb_t *keyword;  /* the second level, or NULL */
    uint8_t nparams;
    const char *params[DCL_PARAM_MAX];
    int32_t numbers[DCL_PARAM_MAX];  /* DCL_VT_NUMBER parameters */
    uint8_t nquals;
    dcl_qval_t quals[DCL_QUAL_MAX];
    /* On error. */
    const char *token;          /* the offending token, or NULL */
    const char *prompt;         /* DCL_INSFPRM: the prompt of the first missing parameter */
    char text[DCL_TEXT_SIZE];   /* processed tokens */
} dcl_cmd_t;

/*
 * Parses a complete line (no continuation) against table. Returns
 * DCL_NORMAL with cmd filled, or one error with cmd->token set where a
 * token caused it. On DCL_INSFPRM, cmd->verb, cmd->keyword and the
 * parameters so far are valid and cmd->prompt names what to ask for; the
 * caller may append the answer to the line and parse again.
 * A qualifier given twice keeps the last occurrence.
 * Qualifiers are looked up among the keyword's first, then the verb's.
 */
dcl_msg_t dcl_parse(const char *line, const dcl_table_t *table, dcl_cmd_t *cmd);

/* The qualifier as given on the line, by full name, or NULL. */
const dcl_qval_t *dcl_cmd_qual(const dcl_cmd_t *cmd, const char *name);
/* 1 when given and not negated, or not given and DCL_QF_DEFON; else 0. */
int dcl_cmd_on(const dcl_cmd_t *cmd, const char *name);
/* The first value, or the default when the qualifier is on without a value; else NULL. */
const char *dcl_cmd_value(const dcl_cmd_t *cmd, const char *name);
/* The privileges the parsed command needs. */
uint32_t dcl_cmd_privs(const dcl_cmd_t *cmd);
/* DCL_NORMAL when have covers the command's needs, else DCL_SYS_NOPRIV. */
dcl_msg_t dcl_check_privs(const dcl_cmd_t *cmd, uint32_t have);

#endif
