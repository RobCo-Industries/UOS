#ifndef UOS_UAF_H
#define UOS_UAF_H

#include <stddef.h>
#include <stdint.h>

#include "sha256.h"

/*
 * The user database, SYSUAF.DAT. Little-endian, fixed width:
 *
 * header, 32 bytes:
 *   0  magic "UOSUAF1\0"     8  version u16 = 1    10 record_size u16 = 96
 *   12 count u16            14 reserved u16 = 0   16 iterations u32
 *   20 crc32 u32 (CRC-32/ISO-HDLC of every record, in order)
 *   24 reserved 8 bytes = 0
 * record, 96 bytes:
 *   0  name char[16]   16 salt u8[16]   32 hash u8[32]   64 privs u32
 *   68 flags u32       72 home char[16] 88 reserved 8 bytes = 0
 *
 * hash = SHA-256(salt || password), then iterations - 1 times
 * hash = SHA-256(hash || salt || password).
 */
#define UAF_MAGIC           "UOSUAF1"   /* 7 characters and a NUL: 8 bytes */
#define UAF_MAGIC_LEN       8
#define UAF_VERSION         1
#define UAF_HEADER_SIZE     32
#define UAF_RECORD_SIZE     96
#define UAF_MAX_USERS       32
#define UAF_MAX_IMAGE       (UAF_HEADER_SIZE + UAF_MAX_USERS * UAF_RECORD_SIZE)
#define UAF_NAME_MAX        16          /* 1 to 16 of A-Z 0-9 $ _ */
#define UAF_HOME_MAX        8           /* 1 to 8 of A-Z 0-9 $ _ - (a FAT name) */
#define UAF_HOME_FIELD      16
#define UAF_SALT_SIZE       16
#define UAF_HASH_SIZE       32
#define UAF_PASSWORD_MIN    1
#define UAF_PASSWORD_MAX    32          /* printable ASCII 0x20 to 0x7E */
#define UAF_ITER_MIN        1
#define UAF_ITER_MAX        100000
#define UAF_ITER_DEFAULT    2000

#define UAF_PRIV_SYSTEM     (1u << 0)
#define UAF_PRIV_OPER       (1u << 1)
#define UAF_PRIV_SECURITY   (1u << 2)
#define UAF_PRIV_READALL    (1u << 3)
#define UAF_PRIV_ALL        0x0Fu
#define UAF_PRIVS_TEXT_MAX  32          /* "SYSTEM,OPER,SECURITY,READALL" + NUL */

#define UAF_FLAG_DISABLED   (1u << 0)
#define UAF_FLAG_NOHACK     (1u << 1)   /* never a minigame target */

typedef enum uaf_err {
    UAF_OK = 0,
    UAF_E_SHORT,            /* image shorter than the header */
    UAF_E_MAGIC,
    UAF_E_VERSION,
    UAF_E_RECSIZE,
    UAF_E_COUNT,            /* count over 32 */
    UAF_E_HDR_RESERVED,     /* a header reserved byte is not zero */
    UAF_E_ITERATIONS,       /* iterations outside 1 to 100000 */
    UAF_E_SIZE,             /* length is not 32 + count * 96 */
    UAF_E_CRC,
    UAF_E_NAME,             /* empty name or a character outside A-Z 0-9 $ _ */
    UAF_E_NAME_PAD,         /* a non-NUL byte after the name's NUL */
    UAF_E_HOME,             /* empty or bad home, or over 8 characters */
    UAF_E_HOME_PAD,
    UAF_E_REC_RESERVED,     /* a record reserved byte is not zero */
    UAF_E_DUPLICATE,        /* two records with one name */
    UAF_E_PASSWORD_LEN,     /* password not 1 to 32 characters */
    UAF_E_PASSWORD_CHAR,    /* password character outside 0x20 to 0x7E */
    UAF_E_NO_USER,
    UAF_E_EXISTS,
    UAF_E_FULL,             /* 32 users already */
    UAF_E_BUFFER,           /* output buffer too small */
    UAF_E_PRIV,             /* unknown privilege name or bad list */
    UAF_E_BAD_LOGIN,        /* unknown user or wrong password: one code for both */
    UAF_E_DISABLED,         /* right password, disabled account */
    UAF_E_NUM
} uaf_err_t;

typedef struct uaf_rec {
    char name[UAF_NAME_MAX];        /* upper-case, NUL padded, no NUL when 16 long */
    uint8_t salt[UAF_SALT_SIZE];
    uint8_t hash[UAF_HASH_SIZE];
    uint32_t privs;
    uint32_t flags;
    char home[UAF_HOME_FIELD];      /* NUL padded */
} uaf_rec_t;

/* A database in memory, about 3 KiB. The caller owns it. */
typedef struct uaf_db {
    uint32_t iterations;
    uint32_t count;
    uaf_rec_t rec[UAF_MAX_USERS];
} uaf_db_t;

/* Work done by a hash, for tests: SHA-256 messages and compressions. */
typedef struct uaf_work {
    uint32_t hashes;
    uint32_t blocks;
} uaf_work_t;

/*
 * Checks an image. Returns the first failed check in this order: header,
 * size, CRC, then each record. *bad_rec (may be NULL) gets the record
 * index for record errors, else -1.
 */
uaf_err_t uaf_validate(const void *img, size_t len, int *bad_rec);
/* Validates, then fills db. db is untouched on an error. */
uaf_err_t uaf_load(uaf_db_t *db, const void *img, size_t len, int *bad_rec);
/* An empty database with `iterations`. */
uaf_err_t uaf_db_init(uaf_db_t *db, uint32_t iterations);
/*
 * Writes db as an image with a fresh CRC. *out_len gets the size,
 * 32 + count * 96. UAF_E_BUFFER, nothing written, when cap is smaller.
 */
uaf_err_t uaf_write(const uaf_db_t *db, void *out, size_t cap, size_t *out_len);

/* Name rules: 1 to 16 characters of A-Z a-z 0-9 $ _; input case is ignored. */
uaf_err_t uaf_check_name(const char *name);
/* Home rules: 1 to 8 characters of A-Z a-z 0-9 $ _ -. */
uaf_err_t uaf_check_home(const char *home);
/* Password rules: 1 to 32 characters, each 0x20 to 0x7E. */
uaf_err_t uaf_check_password(const char *pw);

/* Index of the user, name compared case-insensitively, or -1. */
int uaf_find(const uaf_db_t *db, const char *name);

/*
 * The iterated hash. pw must pass uaf_check_password (else UAF_E_PASSWORD_*
 * and out is zeroed); iterations 1 to 100000 (else UAF_E_ITERATIONS).
 * work (may be NULL) is added to.
 */
uaf_err_t uaf_hash(const uint8_t salt[UAF_SALT_SIZE], const char *pw, uint32_t iterations,
                   uint8_t out[UAF_HASH_SIZE], uaf_work_t *work);
/* Compares 32 bytes without stopping early. 1 if equal. */
int uaf_hash_equal(const uint8_t a[UAF_HASH_SIZE], const uint8_t b[UAF_HASH_SIZE]);
/*
 * Checks a login. Always runs the full iteration count: an unknown user is
 * hashed against a dummy record, and a bad password is padded the same way.
 * Returns UAF_OK, UAF_E_BAD_LOGIN or UAF_E_DISABLED (the password was
 * right). *index (may be NULL) gets the user on UAF_OK and UAF_E_DISABLED,
 * else -1. work (may be NULL) is added to.
 */
uaf_err_t uaf_verify(const uaf_db_t *db, const char *name, const char *pw, int *index,
                     uaf_work_t *work);

/* AUTHORIZE operations. Names are stored upper-case. */
uaf_err_t uaf_add(uaf_db_t *db, const char *name, const char *home, uint32_t privs,
                  uint32_t flags, const uint8_t salt[UAF_SALT_SIZE], const char *pw);
/* Removes the user and keeps the order of the others. */
uaf_err_t uaf_remove(uaf_db_t *db, const char *name);
uaf_err_t uaf_set_password(uaf_db_t *db, const char *name, const uint8_t salt[UAF_SALT_SIZE],
                           const char *pw);
uaf_err_t uaf_set_privs(uaf_db_t *db, const char *name, uint32_t privs);
uaf_err_t uaf_set_flags(uaf_db_t *db, const char *name, uint32_t flags);
uaf_err_t uaf_set_home(uaf_db_t *db, const char *name, const char *home);

/*
 * "SYSTEM,OPER" style text in bit order, "" for none. Bits outside the
 * four are ignored. UAF_E_BUFFER, buf set to "" when cap > 0, if too small.
 */
uaf_err_t uaf_privs_format(uint32_t privs, char *buf, size_t cap);
/*
 * Parses a comma list, case-insensitive, spaces around names allowed,
 * optional surrounding parentheses, "" or "()" is none. Any name may be
 * shortened to a unique prefix. UAF_E_PRIV on an unknown or empty item.
 */
uaf_err_t uaf_privs_parse(const char *text, uint32_t *privs);
/* Name of one privilege bit 0 to 3, or NULL. */
const char *uaf_priv_name(unsigned bit);

const char *uaf_strerror(uaf_err_t err);

#endif
