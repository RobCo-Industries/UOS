#include "uaf.h"

#include <robco/crc32.h>
#include <robco/klibc.h>

static const char *const uaf_priv_names[4] = { "SYSTEM", "OPER", "SECURITY", "READALL" };

static uint16_t get16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void put16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static char upper(char c)
{
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

static int is_name_char(char c, int allow_dash)
{
    c = upper(c);
    return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '$' || c == '_' ||
           (allow_dash && c == '-');
}

/* Checks a NUL-terminated string against the alphabet and a maximum. */
static int check_word(const char *s, size_t max, int allow_dash)
{
    size_t n = 0;

    if (s == NULL) {
        return 0;
    }
    while (s[n] != '\0') {
        if (n >= max || !is_name_char(s[n], allow_dash)) {
            return 0;
        }
        n++;
    }
    return n > 0;
}

uaf_err_t uaf_check_name(const char *name)
{
    return check_word(name, UAF_NAME_MAX, 0) ? UAF_OK : UAF_E_NAME;
}

uaf_err_t uaf_check_home(const char *home)
{
    return check_word(home, UAF_HOME_MAX, 1) ? UAF_OK : UAF_E_HOME;
}

uaf_err_t uaf_check_password(const char *pw)
{
    size_t n = 0;

    if (pw == NULL) {
        return UAF_E_PASSWORD_LEN;
    }
    while (pw[n] != '\0') {
        if (n >= UAF_PASSWORD_MAX) {
            return UAF_E_PASSWORD_LEN;
        }
        if ((unsigned char)pw[n] < 0x20 || (unsigned char)pw[n] > 0x7E) {
            return UAF_E_PASSWORD_CHAR;
        }
        n++;
    }
    return n == 0 ? UAF_E_PASSWORD_LEN : UAF_OK;
}

/*
 * Checks a fixed field holding upper-case word characters, NUL padded.
 * Returns 0 if good, 1 for a bad character or length, 2 for bad padding.
 */
static int check_field(const uint8_t *f, size_t field, size_t max, int allow_dash)
{
    size_t n = 0;
    size_t i;

    while (n < field && f[n] != 0) {
        char c = (char)f[n];

        if (n >= max || !is_name_char(c, allow_dash) || (c >= 'a' && c <= 'z')) {
            return 1;
        }
        n++;
    }
    if (n == 0) {
        return 1;
    }
    for (i = n; i < field; i++) {
        if (f[i] != 0) {
            return 2;
        }
    }
    return 0;
}

static int all_zero(const uint8_t *p, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++) {
        if (p[i] != 0) {
            return 0;
        }
    }
    return 1;
}

uaf_err_t uaf_validate(const void *img, size_t len, int *bad_rec)
{
    const uint8_t *p = img;
    uint32_t count, iter, i, j;

    if (bad_rec != NULL) {
        *bad_rec = -1;
    }
    if (p == NULL || len < UAF_HEADER_SIZE) {
        return UAF_E_SHORT;
    }
    if (k_memcmp(p, UAF_MAGIC, UAF_MAGIC_LEN) != 0) {
        return UAF_E_MAGIC;
    }
    if (get16(p + 8) != UAF_VERSION) {
        return UAF_E_VERSION;
    }
    if (get16(p + 10) != UAF_RECORD_SIZE) {
        return UAF_E_RECSIZE;
    }
    count = get16(p + 12);
    if (count > UAF_MAX_USERS) {
        return UAF_E_COUNT;
    }
    if (get16(p + 14) != 0 || !all_zero(p + 24, 8)) {
        return UAF_E_HDR_RESERVED;
    }
    iter = get32(p + 16);
    if (iter < UAF_ITER_MIN || iter > UAF_ITER_MAX) {
        return UAF_E_ITERATIONS;
    }
    if (len != UAF_HEADER_SIZE + (size_t)count * UAF_RECORD_SIZE) {
        return UAF_E_SIZE;
    }
    if (crc32(p + UAF_HEADER_SIZE, (size_t)count * UAF_RECORD_SIZE) != get32(p + 20)) {
        return UAF_E_CRC;
    }
    for (i = 0; i < count; i++) {
        const uint8_t *r = p + UAF_HEADER_SIZE + i * UAF_RECORD_SIZE;
        uaf_err_t err = UAF_OK;
        int c;

        c = check_field(r, UAF_NAME_MAX, UAF_NAME_MAX, 0);
        if (c != 0) {
            err = c == 1 ? UAF_E_NAME : UAF_E_NAME_PAD;
        } else if ((c = check_field(r + 72, UAF_HOME_FIELD, UAF_HOME_MAX, 1)) != 0) {
            err = c == 1 ? UAF_E_HOME : UAF_E_HOME_PAD;
        } else if (!all_zero(r + 88, 8)) {
            err = UAF_E_REC_RESERVED;
        } else {
            for (j = 0; j < i; j++) {
                if (k_memcmp(r, p + UAF_HEADER_SIZE + j * UAF_RECORD_SIZE, UAF_NAME_MAX) == 0) {
                    err = UAF_E_DUPLICATE;
                    break;
                }
            }
        }
        if (err != UAF_OK) {
            if (bad_rec != NULL) {
                *bad_rec = (int)i;
            }
            return err;
        }
    }
    return UAF_OK;
}

uaf_err_t uaf_db_init(uaf_db_t *db, uint32_t iterations)
{
    if (iterations < UAF_ITER_MIN || iterations > UAF_ITER_MAX) {
        return UAF_E_ITERATIONS;
    }
    k_memset(db, 0, sizeof(*db));
    db->iterations = iterations;
    return UAF_OK;
}

uaf_err_t uaf_load(uaf_db_t *db, const void *img, size_t len, int *bad_rec)
{
    const uint8_t *p = img;
    uaf_err_t err = uaf_validate(img, len, bad_rec);
    uint32_t i;

    if (err != UAF_OK) {
        return err;
    }
    k_memset(db, 0, sizeof(*db));
    db->iterations = get32(p + 16);
    db->count = get16(p + 12);
    for (i = 0; i < db->count; i++) {
        const uint8_t *r = p + UAF_HEADER_SIZE + i * UAF_RECORD_SIZE;
        uaf_rec_t *u = &db->rec[i];

        k_memcpy(u->name, r, UAF_NAME_MAX);
        k_memcpy(u->salt, r + 16, UAF_SALT_SIZE);
        k_memcpy(u->hash, r + 32, UAF_HASH_SIZE);
        u->privs = get32(r + 64);
        u->flags = get32(r + 68);
        k_memcpy(u->home, r + 72, UAF_HOME_FIELD);
    }
    return UAF_OK;
}

uaf_err_t uaf_write(const uaf_db_t *db, void *out, size_t cap, size_t *out_len)
{
    uint8_t *p = out;
    size_t need = UAF_HEADER_SIZE + (size_t)db->count * UAF_RECORD_SIZE;
    uint32_t i;

    if (db->count > UAF_MAX_USERS) {
        return UAF_E_COUNT;
    }
    if (db->iterations < UAF_ITER_MIN || db->iterations > UAF_ITER_MAX) {
        return UAF_E_ITERATIONS;
    }
    if (out == NULL || cap < need) {
        return UAF_E_BUFFER;
    }
    k_memset(p, 0, need);
    k_memcpy(p, UAF_MAGIC, UAF_MAGIC_LEN);
    put16(p + 8, UAF_VERSION);
    put16(p + 10, UAF_RECORD_SIZE);
    put16(p + 12, db->count);
    put32(p + 16, db->iterations);
    for (i = 0; i < db->count; i++) {
        uint8_t *r = p + UAF_HEADER_SIZE + i * UAF_RECORD_SIZE;
        const uaf_rec_t *u = &db->rec[i];

        k_memcpy(r, u->name, UAF_NAME_MAX);
        k_memcpy(r + 16, u->salt, UAF_SALT_SIZE);
        k_memcpy(r + 32, u->hash, UAF_HASH_SIZE);
        put32(r + 64, u->privs);
        put32(r + 68, u->flags);
        k_memcpy(r + 72, u->home, UAF_HOME_FIELD);
    }
    put32(p + 20, crc32(p + UAF_HEADER_SIZE, (size_t)db->count * UAF_RECORD_SIZE));
    if (out_len != NULL) {
        *out_len = need;
    }
    return UAF_OK;
}

/* Upper-cases a checked word into a NUL-padded field. */
static void store_word(char *field, size_t size, const char *s)
{
    size_t i;

    k_memset(field, 0, size);
    for (i = 0; s[i] != '\0' && i < size; i++) {
        field[i] = upper(s[i]);
    }
}

int uaf_find(const uaf_db_t *db, const char *name)
{
    char key[UAF_NAME_MAX];
    uint32_t i;

    if (uaf_check_name(name) != UAF_OK) {
        return -1;
    }
    store_word(key, sizeof(key), name);
    for (i = 0; i < db->count; i++) {
        if (k_memcmp(db->rec[i].name, key, UAF_NAME_MAX) == 0) {
            return (int)i;
        }
    }
    return -1;
}

/* The hash with no checks; pw is 0 to 32 bytes. */
static void hash_raw(const uint8_t salt[UAF_SALT_SIZE], const char *pw, size_t pw_len,
                     uint32_t iterations, uint8_t out[UAF_HASH_SIZE], uaf_work_t *work)
{
    sha_ctx_t ctx;
    uint32_t i;

    sha_init(&ctx);
    sha_update(&ctx, salt, UAF_SALT_SIZE);
    sha_update(&ctx, pw, pw_len);
    sha_final(&ctx, out);
    if (work != NULL) {
        work->hashes++;
        work->blocks += ctx.blocks;
    }
    for (i = 1; i < iterations; i++) {
        sha_init(&ctx);
        sha_update(&ctx, out, UAF_HASH_SIZE);
        sha_update(&ctx, salt, UAF_SALT_SIZE);
        sha_update(&ctx, pw, pw_len);
        sha_final(&ctx, out);
        if (work != NULL) {
            work->hashes++;
            work->blocks += ctx.blocks;
        }
    }
    k_memset(&ctx, 0, sizeof(ctx));
}

uaf_err_t uaf_hash(const uint8_t salt[UAF_SALT_SIZE], const char *pw, uint32_t iterations,
                   uint8_t out[UAF_HASH_SIZE], uaf_work_t *work)
{
    uaf_err_t err = uaf_check_password(pw);

    if (err == UAF_OK && (iterations < UAF_ITER_MIN || iterations > UAF_ITER_MAX)) {
        err = UAF_E_ITERATIONS;
    }
    if (err != UAF_OK) {
        k_memset(out, 0, UAF_HASH_SIZE);
        return err;
    }
    hash_raw(salt, pw, k_strlen(pw), iterations, out, work);
    return UAF_OK;
}

int uaf_hash_equal(const uint8_t a[UAF_HASH_SIZE], const uint8_t b[UAF_HASH_SIZE])
{
    volatile uint8_t diff = 0;
    size_t i;

    for (i = 0; i < UAF_HASH_SIZE; i++) {
        diff |= (uint8_t)(a[i] ^ b[i]);
    }
    return diff == 0;
}

uaf_err_t uaf_verify(const uaf_db_t *db, const char *name, const char *pw, int *index,
                     uaf_work_t *work)
{
    /* Hashed in place of a password that breaks the rules: the longest. */
    static const char dummy_pw[] = "................................";
    static const uint8_t dummy_salt[UAF_SALT_SIZE];
    static const uint8_t dummy_hash[UAF_HASH_SIZE];
    const uint8_t *salt = dummy_salt;
    const uint8_t *want = dummy_hash;
    uint8_t got[UAF_HASH_SIZE];
    uint32_t iter = db->iterations;
    int idx = uaf_find(db, name);
    int pw_ok = uaf_check_password(pw) == UAF_OK;
    int equal;

    if (iter < UAF_ITER_MIN || iter > UAF_ITER_MAX) {
        iter = UAF_ITER_DEFAULT;
    }
    if (idx >= 0) {
        salt = db->rec[idx].salt;
        want = db->rec[idx].hash;
    }
    if (pw_ok) {
        hash_raw(salt, pw, k_strlen(pw), iter, got, work);
    } else {
        hash_raw(salt, dummy_pw, UAF_PASSWORD_MAX, iter, got, work);
    }
    equal = uaf_hash_equal(got, want);
    k_memset(got, 0, sizeof(got));
    if (index != NULL) {
        *index = -1;
    }
    if (idx < 0 || !pw_ok || !equal) {
        return UAF_E_BAD_LOGIN;
    }
    if (index != NULL) {
        *index = idx;
    }
    return (db->rec[idx].flags & UAF_FLAG_DISABLED) ? UAF_E_DISABLED : UAF_OK;
}

uaf_err_t uaf_add(uaf_db_t *db, const char *name, const char *home, uint32_t privs,
                  uint32_t flags, const uint8_t salt[UAF_SALT_SIZE], const char *pw)
{
    uaf_rec_t *u;
    uaf_err_t err;

    if ((err = uaf_check_name(name)) != UAF_OK || (err = uaf_check_home(home)) != UAF_OK ||
        (err = uaf_check_password(pw)) != UAF_OK) {
        return err;
    }
    if (uaf_find(db, name) >= 0) {
        return UAF_E_EXISTS;
    }
    if (db->count >= UAF_MAX_USERS) {
        return UAF_E_FULL;
    }
    u = &db->rec[db->count];
    k_memset(u, 0, sizeof(*u));
    store_word(u->name, sizeof(u->name), name);
    store_word(u->home, sizeof(u->home), home);
    k_memcpy(u->salt, salt, UAF_SALT_SIZE);
    u->privs = privs;
    u->flags = flags;
    err = uaf_hash(u->salt, pw, db->iterations, u->hash, NULL);
    if (err != UAF_OK) {
        return err;
    }
    db->count++;
    return UAF_OK;
}

static uaf_err_t find_or_err(const uaf_db_t *db, const char *name, int *idx)
{
    if (uaf_check_name(name) != UAF_OK) {
        return UAF_E_NAME;
    }
    *idx = uaf_find(db, name);
    return *idx < 0 ? UAF_E_NO_USER : UAF_OK;
}

uaf_err_t uaf_remove(uaf_db_t *db, const char *name)
{
    int idx;
    uaf_err_t err = find_or_err(db, name, &idx);

    if (err != UAF_OK) {
        return err;
    }
    k_memmove(&db->rec[idx], &db->rec[idx + 1], (db->count - (uint32_t)idx - 1) * sizeof(uaf_rec_t));
    db->count--;
    k_memset(&db->rec[db->count], 0, sizeof(uaf_rec_t));
    return UAF_OK;
}

uaf_err_t uaf_set_password(uaf_db_t *db, const char *name, const uint8_t salt[UAF_SALT_SIZE],
                           const char *pw)
{
    uint8_t h[UAF_HASH_SIZE];
    int idx;
    uaf_err_t err = find_or_err(db, name, &idx);

    if (err != UAF_OK) {
        return err;
    }
    err = uaf_hash(salt, pw, db->iterations, h, NULL);
    if (err != UAF_OK) {
        return err;
    }
    k_memcpy(db->rec[idx].salt, salt, UAF_SALT_SIZE);
    k_memcpy(db->rec[idx].hash, h, UAF_HASH_SIZE);
    return UAF_OK;
}

uaf_err_t uaf_set_privs(uaf_db_t *db, const char *name, uint32_t privs)
{
    int idx;
    uaf_err_t err = find_or_err(db, name, &idx);

    if (err == UAF_OK) {
        db->rec[idx].privs = privs;
    }
    return err;
}

uaf_err_t uaf_set_flags(uaf_db_t *db, const char *name, uint32_t flags)
{
    int idx;
    uaf_err_t err = find_or_err(db, name, &idx);

    if (err == UAF_OK) {
        db->rec[idx].flags = flags;
    }
    return err;
}

uaf_err_t uaf_set_home(uaf_db_t *db, const char *name, const char *home)
{
    int idx;
    uaf_err_t err = find_or_err(db, name, &idx);

    if (err == UAF_OK && (err = uaf_check_home(home)) == UAF_OK) {
        store_word(db->rec[idx].home, sizeof(db->rec[idx].home), home);
    }
    return err;
}

const char *uaf_priv_name(unsigned bit)
{
    return bit < 4 ? uaf_priv_names[bit] : NULL;
}

uaf_err_t uaf_privs_format(uint32_t privs, char *buf, size_t cap)
{
    size_t n = 0;
    unsigned bit;

    if (buf == NULL || cap == 0) {
        return UAF_E_BUFFER;
    }
    buf[0] = '\0';
    for (bit = 0; bit < 4; bit++) {
        size_t len;

        if (!(privs & (1u << bit))) {
            continue;
        }
        len = k_strlen(uaf_priv_names[bit]);
        if (n + (n > 0) + len + 1 > cap) {
            buf[0] = '\0';
            return UAF_E_BUFFER;
        }
        if (n > 0) {
            buf[n++] = ',';
        }
        k_memcpy(buf + n, uaf_priv_names[bit], len);
        n += len;
        buf[n] = '\0';
    }
    return UAF_OK;
}

/* Matches s[0..len) as a unique prefix of one privilege name. */
static int priv_lookup(const char *s, size_t len)
{
    int found = -1;
    unsigned bit;
    size_t i;

    if (len == 0) {
        return -1;
    }
    for (bit = 0; bit < 4; bit++) {
        const char *n = uaf_priv_names[bit];

        if (len > k_strlen(n)) {
            continue;
        }
        for (i = 0; i < len && upper(s[i]) == n[i]; i++) {
        }
        if (i == len) {
            if (len == k_strlen(n)) {
                return (int)bit;
            }
            if (found >= 0) {
                return -2;
            }
            found = (int)bit;
        }
    }
    return found;
}

uaf_err_t uaf_privs_parse(const char *text, uint32_t *privs)
{
    size_t start, end, len;
    uint32_t bits = 0;

    if (text == NULL) {
        return UAF_E_PRIV;
    }
    len = k_strlen(text);
    start = 0;
    while (start < len && text[start] == ' ') {
        start++;
    }
    while (len > start && text[len - 1] == ' ') {
        len--;
    }
    if (start < len && text[start] == '(') {
        if (text[len - 1] != ')' || len - start < 2) {
            return UAF_E_PRIV;
        }
        start++;
        len--;
    }
    while (start < len && text[start] == ' ') {
        start++;
    }
    if (start == len) {
        *privs = 0;
        return UAF_OK;
    }
    for (;;) {
        size_t a, b;
        int bit;

        end = start;
        while (end < len && text[end] != ',') {
            end++;
        }
        a = start;
        b = end;
        while (a < b && text[a] == ' ') {
            a++;
        }
        while (b > a && text[b - 1] == ' ') {
            b--;
        }
        bit = priv_lookup(text + a, b - a);
        if (bit < 0) {
            return UAF_E_PRIV;
        }
        bits |= 1u << bit;
        if (end == len) {
            break;
        }
        start = end + 1;
    }
    *privs = bits;
    return UAF_OK;
}

const char *uaf_strerror(uaf_err_t err)
{
    static const char *const text[UAF_E_NUM] = {
        [UAF_OK] = "ok",
        [UAF_E_SHORT] = "user database shorter than its header",
        [UAF_E_MAGIC] = "user database has a bad magic",
        [UAF_E_VERSION] = "user database version is not 1",
        [UAF_E_RECSIZE] = "user database record size is not 96",
        [UAF_E_COUNT] = "user database holds more than 32 users",
        [UAF_E_HDR_RESERVED] = "user database header reserved bytes are not zero",
        [UAF_E_ITERATIONS] = "iterations must be 1 to 100000",
        [UAF_E_SIZE] = "user database size does not match its count",
        [UAF_E_CRC] = "user database CRC mismatch",
        [UAF_E_NAME] = "user name must be 1 to 16 of A-Z 0-9 $ _",
        [UAF_E_NAME_PAD] = "user name padding is not zero",
        [UAF_E_HOME] = "home must be 1 to 8 of A-Z 0-9 $ _ -",
        [UAF_E_HOME_PAD] = "home padding is not zero",
        [UAF_E_REC_RESERVED] = "user record reserved bytes are not zero",
        [UAF_E_DUPLICATE] = "user name appears twice",
        [UAF_E_PASSWORD_LEN] = "password must be 1 to 32 characters",
        [UAF_E_PASSWORD_CHAR] = "password must be printable ASCII",
        [UAF_E_NO_USER] = "no such user",
        [UAF_E_EXISTS] = "user already exists",
        [UAF_E_FULL] = "user database is full",
        [UAF_E_BUFFER] = "buffer too small",
        [UAF_E_PRIV] = "unknown or ambiguous privilege",
        [UAF_E_BAD_LOGIN] = "user authorization failure",
        [UAF_E_DISABLED] = "account is disabled",
    };

    if ((unsigned)err >= UAF_E_NUM) {
        return "unknown user database error";
    }
    return text[err];
}
