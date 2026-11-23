#include "uaf.h"

#include <robco/crc32.h>

#include "rc_test.h"
#include "uaf_vectors.h"

static uint8_t img[UAF_MAX_IMAGE + 96];

static void unhex(const char *s, uint8_t *out, size_t n)
{
    size_t i;
    unsigned v;

    for (i = 0; i < n; i++) {
        sscanf(s + 2 * i, "%2x", &v);
        out[i] = (uint8_t)v;
    }
}

static void salt_of(uint8_t salt[16], uint8_t seed)
{
    for (int i = 0; i < 16; i++) {
        salt[i] = (uint8_t)(seed + i);
    }
}

/* A database of three users at 3 iterations. */
static void make_db(uaf_db_t *db)
{
    uint8_t s[16];

    CHECK_EQ(uaf_db_init(db, 3), UAF_OK);
    salt_of(s, 1);
    CHECK_EQ(uaf_add(db, "admin", "ADMIN", UAF_PRIV_SYSTEM, UAF_FLAG_NOHACK, s, "Secret1"), UAF_OK);
    salt_of(s, 2);
    CHECK_EQ(uaf_add(db, "OPER", "OPS", UAF_PRIV_OPER, 0, s, "op"), UAF_OK);
    salt_of(s, 3);
    CHECK_EQ(uaf_add(db, "GUEST", "GUEST", 0, 0, s, "guest"), UAF_OK);
}

static size_t make_img(void)
{
    uaf_db_t db;
    size_t len = 0;

    make_db(&db);
    CHECK_EQ(uaf_write(&db, img, sizeof(img), &len), UAF_OK);
    return len;
}

static void fix_crc(size_t len)
{
    uint32_t c = crc32(img + 32, len - 32);

    img[20] = (uint8_t)c;
    img[21] = (uint8_t)(c >> 8);
    img[22] = (uint8_t)(c >> 16);
    img[23] = (uint8_t)(c >> 24);
}

TEST(shared_vectors)
{
    size_t i;

    CHECK(sizeof(uaf_vectors) / sizeof(uaf_vectors[0]) >= 8);
    for (i = 0; i < sizeof(uaf_vectors) / sizeof(uaf_vectors[0]); i++) {
        const struct uaf_vector *v = &uaf_vectors[i];
        uint8_t salt[16], want[32], got[32];

        unhex(v->salt, salt, 16);
        unhex(v->hash, want, 32);
        CHECK_EQ(uaf_check_name(v->name), UAF_OK);
        CHECK_EQ(uaf_hash(salt, v->password, v->iterations, got, NULL), UAF_OK);
        CHECK_MEM(got, want, 32);
    }
}

TEST(layout_and_roundtrip)
{
    size_t len = make_img();
    uaf_db_t db;
    int bad = 99;

    CHECK_EQ(len, 32 + 3 * 96);
    CHECK_MEM(img, "UOSUAF1\0", 8);
    CHECK_EQ(img[8], 1);
    CHECK_EQ(img[10], 96);
    CHECK_EQ(img[12], 3);
    CHECK_EQ(img[16], 3);
    CHECK_MEM(img + 32, "ADMIN\0\0\0\0\0\0\0\0\0\0\0", 16);
    CHECK_EQ(img[32 + 16], 1);                 /* salt */
    CHECK_EQ(img[32 + 64], UAF_PRIV_SYSTEM);   /* privs */
    CHECK_EQ(img[32 + 68], UAF_FLAG_NOHACK);   /* flags */
    CHECK_MEM(img + 32 + 72, "ADMIN", 6);
    CHECK_EQ(uaf_validate(img, len, &bad), UAF_OK);
    CHECK_EQ(bad, -1);
    CHECK_EQ(uaf_load(&db, img, len, NULL), UAF_OK);
    CHECK_EQ(db.count, 3);
    CHECK_EQ(db.iterations, 3);
    CHECK_EQ(uaf_find(&db, "oPeR"), 1);
    CHECK_STR(db.rec[1].home, "OPS");
}

TEST(write_buffer_too_small)
{
    uaf_db_t db;
    size_t len = 7;

    make_db(&db);
    memset(img, 0xAA, sizeof(img));
    CHECK_EQ(uaf_write(&db, img, 32 + 3 * 96 - 1, &len), UAF_E_BUFFER);
    CHECK_EQ(len, 7);
    CHECK_EQ(img[0], 0xAA);
    CHECK_EQ(uaf_write(&db, img, 32 + 3 * 96, &len), UAF_OK);
    db.iterations = 0;
    CHECK_EQ(uaf_write(&db, img, sizeof(img), &len), UAF_E_ITERATIONS);
    db.iterations = 3;
    db.count = 33;
    CHECK_EQ(uaf_write(&db, img, sizeof(img), &len), UAF_E_COUNT);
}

TEST(validate_header_checks)
{
    size_t len = make_img();

    CHECK_EQ(uaf_validate(img, 31, NULL), UAF_E_SHORT);
    CHECK_EQ(uaf_validate(NULL, 0, NULL), UAF_E_SHORT);
    img[6] = 'X';
    CHECK_EQ(uaf_validate(img, len, NULL), UAF_E_MAGIC);
    len = make_img();
    img[7] = 'X';                                   /* the magic's NUL */
    CHECK_EQ(uaf_validate(img, len, NULL), UAF_E_MAGIC);
    len = make_img();
    img[8] = 2;
    CHECK_EQ(uaf_validate(img, len, NULL), UAF_E_VERSION);
    len = make_img();
    img[10] = 95;
    CHECK_EQ(uaf_validate(img, len, NULL), UAF_E_RECSIZE);
    len = make_img();
    img[12] = 33;
    CHECK_EQ(uaf_validate(img, len, NULL), UAF_E_COUNT);
    len = make_img();
    img[15] = 1;
    CHECK_EQ(uaf_validate(img, len, NULL), UAF_E_HDR_RESERVED);
    len = make_img();
    img[31] = 1;
    CHECK_EQ(uaf_validate(img, len, NULL), UAF_E_HDR_RESERVED);
    len = make_img();
    img[16] = 0;
    CHECK_EQ(uaf_validate(img, len, NULL), UAF_E_ITERATIONS);
    img[16] = 0xA1;                                 /* 100001 = 0x186A1 */
    img[17] = 0x86;
    img[18] = 0x01;
    CHECK_EQ(uaf_validate(img, len, NULL), UAF_E_ITERATIONS);
    img[16] = 0xA0;                                 /* 100000 */
    CHECK_EQ(uaf_validate(img, len, NULL), UAF_OK);
    CHECK_EQ(uaf_validate(img, len - 1, NULL), UAF_E_SIZE);
    CHECK_EQ(uaf_validate(img, len + 1, NULL), UAF_E_SIZE);
    img[40] ^= 1;
    CHECK_EQ(uaf_validate(img, len, NULL), UAF_E_CRC);
    img[40] ^= 1;
    img[20] ^= 1;
    CHECK_EQ(uaf_validate(img, len, NULL), UAF_E_CRC);
}

TEST(validate_empty_and_full)
{
    uaf_db_t db;
    uint8_t s[16] = { 0 };
    char name[13]; /* "U", an int of up to 11 characters, the NUL */
    size_t len;
    int i;

    uaf_db_init(&db, 1);
    CHECK_EQ(uaf_write(&db, img, sizeof(img), &len), UAF_OK);
    CHECK_EQ(len, 32);
    CHECK_EQ(uaf_validate(img, len, NULL), UAF_OK);
    for (i = 0; i < UAF_MAX_USERS; i++) {
        snprintf(name, sizeof(name), "U%d", i);
        CHECK_EQ(uaf_add(&db, name, "H", 0, 0, s, "x"), UAF_OK);
    }
    CHECK_EQ(uaf_add(&db, "ONEMORE", "H", 0, 0, s, "x"), UAF_E_FULL);
    CHECK_EQ(uaf_write(&db, img, sizeof(img), &len), UAF_OK);
    CHECK_EQ(len, UAF_MAX_IMAGE);
    CHECK_EQ(uaf_validate(img, len, NULL), UAF_OK);
}

/* Breaks record `rec` at `off` with `val`, fixes the CRC, expects `err`. */
static void rec_break(int rec, size_t off, uint8_t val, uaf_err_t err)
{
    size_t len = make_img();
    int bad = -5;

    img[32 + rec * 96 + off] = val;
    fix_crc(len);
    CHECK_EQ(uaf_validate(img, len, &bad), err);
    CHECK_EQ(bad, err == UAF_OK ? -1 : rec);
}

TEST(validate_records)
{
    rec_break(0, 0, 0, UAF_E_NAME);             /* empty name */
    rec_break(1, 0, 'o', UAF_E_NAME);           /* lower case stored */
    rec_break(2, 1, '-', UAF_E_NAME);           /* '-' not in a name */
    rec_break(2, 1, 0x80, UAF_E_NAME);
    rec_break(0, 15, 'X', UAF_E_NAME_PAD);      /* ADMIN\0...X */
    rec_break(0, 72, 0, UAF_E_HOME);
    rec_break(0, 72, 'a', UAF_E_HOME);
    rec_break(1, 72 + 3, 'X', UAF_OK);          /* OPSX is fine */
    rec_break(1, 72 + 3, '-', UAF_OK);
    rec_break(0, 72 + 8, 'X', UAF_E_HOME_PAD);  /* after the NUL */
    rec_break(0, 88, 1, UAF_E_REC_RESERVED);
    rec_break(2, 95, 1, UAF_E_REC_RESERVED);
    rec_break(0, 16, 0xFF, UAF_OK);             /* salt bytes are free */
}

TEST(validate_long_fields)
{
    size_t len = make_img();
    int bad;

    memcpy(img + 32, "ABCDEFGHIJKLMNOP", 16);   /* 16 characters, no NUL */
    memcpy(img + 32 + 72, "ABCDEFGH", 8);
    fix_crc(len);
    CHECK_EQ(uaf_validate(img, len, &bad), UAF_OK);
    img[32 + 72 + 8] = 'I';                     /* home of 9 */
    fix_crc(len);
    CHECK_EQ(uaf_validate(img, len, &bad), UAF_E_HOME);
}

TEST(validate_duplicate)
{
    size_t len = make_img();
    int bad;

    memcpy(img + 32 + 2 * 96, "OPER\0", 6);
    fix_crc(len);
    CHECK_EQ(uaf_validate(img, len, &bad), UAF_E_DUPLICATE);
    CHECK_EQ(bad, 2);
}

TEST(load_failure_leaves_db)
{
    uaf_db_t db;
    size_t len = make_img();

    memset(&db, 0x5A, sizeof(db));
    img[8] = 9;
    CHECK_EQ(uaf_load(&db, img, len, NULL), UAF_E_VERSION);
    CHECK_EQ(db.count, 0x5A5A5A5Au);
}

TEST(name_rules)
{
    CHECK_EQ(uaf_check_name("A"), UAF_OK);
    CHECK_EQ(uaf_check_name("sys$mgr_1"), UAF_OK);
    CHECK_EQ(uaf_check_name("ABCDEFGHIJKLMNOP"), UAF_OK);
    CHECK_EQ(uaf_check_name("ABCDEFGHIJKLMNOPQ"), UAF_E_NAME);
    CHECK_EQ(uaf_check_name(""), UAF_E_NAME);
    CHECK_EQ(uaf_check_name(NULL), UAF_E_NAME);
    CHECK_EQ(uaf_check_name("A B"), UAF_E_NAME);
    CHECK_EQ(uaf_check_name("A-B"), UAF_E_NAME);
    CHECK_EQ(uaf_check_home("A-B"), UAF_OK);
    CHECK_EQ(uaf_check_home("ABCDEFGH"), UAF_OK);
    CHECK_EQ(uaf_check_home("ABCDEFGHI"), UAF_E_HOME);
    CHECK_EQ(uaf_check_home(""), UAF_E_HOME);
    CHECK_EQ(uaf_check_home("A.B"), UAF_E_HOME);
}

TEST(password_rules)
{
    char pw[40];

    CHECK_EQ(uaf_check_password("x"), UAF_OK);
    CHECK_EQ(uaf_check_password(" "), UAF_OK);
    CHECK_EQ(uaf_check_password("~"), UAF_OK);
    CHECK_EQ(uaf_check_password(""), UAF_E_PASSWORD_LEN);
    CHECK_EQ(uaf_check_password(NULL), UAF_E_PASSWORD_LEN);
    memset(pw, 'a', 32);
    pw[32] = '\0';
    CHECK_EQ(uaf_check_password(pw), UAF_OK);
    pw[32] = 'a';
    pw[33] = '\0';
    CHECK_EQ(uaf_check_password(pw), UAF_E_PASSWORD_LEN);
    CHECK_EQ(uaf_check_password("a\tb"), UAF_E_PASSWORD_CHAR);
    CHECK_EQ(uaf_check_password("a\x7f"), UAF_E_PASSWORD_CHAR);
    CHECK_EQ(uaf_check_password("\xe9t\xe9"), UAF_E_PASSWORD_CHAR);
}

TEST(hash_definition)
{
    /* Two iterations by hand from sha_ calls. */
    uint8_t salt[16], h[32], got[32];
    sha_ctx_t c;

    salt_of(salt, 7);
    sha_init(&c);
    sha_update(&c, salt, 16);
    sha_update(&c, "pw", 2);
    sha_final(&c, h);
    sha_init(&c);
    sha_update(&c, h, 32);
    sha_update(&c, salt, 16);
    sha_update(&c, "pw", 2);
    sha_final(&c, h);
    CHECK_EQ(uaf_hash(salt, "pw", 2, got, NULL), UAF_OK);
    CHECK_MEM(got, h, 32);
    CHECK_EQ(uaf_hash(salt, "pw", 0, got, NULL), UAF_E_ITERATIONS);
    CHECK_EQ(got[0] | got[31], 0);
    CHECK_EQ(uaf_hash(salt, "pw", 100001, got, NULL), UAF_E_ITERATIONS);
    CHECK_EQ(uaf_hash(salt, "", 1, got, NULL), UAF_E_PASSWORD_LEN);
}

TEST(hash_equal_all_bytes)
{
    uint8_t a[32] = { 0 }, b[32] = { 0 };

    CHECK_EQ(uaf_hash_equal(a, b), 1);
    b[0] = 1;
    CHECK_EQ(uaf_hash_equal(a, b), 0);
    b[0] = 0;
    b[31] = 0x80;
    CHECK_EQ(uaf_hash_equal(a, b), 0);
}

TEST(verify_results)
{
    uaf_db_t db;
    int idx = 7;

    make_db(&db);
    CHECK_EQ(uaf_verify(&db, "admin", "Secret1", &idx, NULL), UAF_OK);
    CHECK_EQ(idx, 0);
    CHECK_EQ(uaf_verify(&db, "ADMIN", "secret1", &idx, NULL), UAF_E_BAD_LOGIN);
    CHECK_EQ(idx, -1);
    CHECK_EQ(uaf_verify(&db, "NOBODY", "Secret1", &idx, NULL), UAF_E_BAD_LOGIN);
    CHECK_EQ(uaf_verify(&db, "BAD NAME", "x", &idx, NULL), UAF_E_BAD_LOGIN);
    CHECK_EQ(uaf_verify(&db, "ADMIN", "", &idx, NULL), UAF_E_BAD_LOGIN);
    CHECK_EQ(uaf_verify(&db, "GUEST", "guest", NULL, NULL), UAF_OK);
    CHECK_EQ(uaf_set_flags(&db, "guest", UAF_FLAG_DISABLED), UAF_OK);
    CHECK_EQ(uaf_verify(&db, "GUEST", "guest", &idx, NULL), UAF_E_DISABLED);
    CHECK_EQ(idx, 2);
    CHECK_EQ(uaf_verify(&db, "GUEST", "nope", &idx, NULL), UAF_E_BAD_LOGIN);
}

/* The work does not depend on whether the name exists or the password is right. */
TEST(verify_same_work)
{
    uaf_db_t db;
    uaf_work_t w_ok = { 0, 0 }, w_bad = { 0, 0 }, w_none = { 0, 0 }, w_rule = { 0, 0 };
    uaf_work_t w_long = { 0, 0 };
    char long_pw[33];

    make_db(&db);
    db.iterations = 2000;
    uaf_verify(&db, "OPER", "abcdefgh", NULL, &w_ok);
    uaf_verify(&db, "OPER", "abcdefgX", NULL, &w_bad);
    uaf_verify(&db, "NOSUCHUSER", "abcdefgh", NULL, &w_none);
    CHECK_EQ(w_ok.hashes, 2000);
    CHECK_EQ(w_ok.blocks, 1 + 1999 * 2);       /* 16+8 = 1 block, 32+16+8 = 2 blocks */
    CHECK_EQ(w_bad.hashes, w_ok.hashes);
    CHECK_EQ(w_bad.blocks, w_ok.blocks);
    CHECK_EQ(w_none.hashes, w_ok.hashes);
    CHECK_EQ(w_none.blocks, w_ok.blocks);
    memset(long_pw, 'z', 32);
    long_pw[32] = '\0';
    uaf_verify(&db, "OPER", long_pw, NULL, &w_long);
    uaf_verify(&db, "BAD NAME", "\t", NULL, &w_rule);
    CHECK_EQ(w_rule.hashes, 2000);
    CHECK_EQ(w_rule.blocks, w_long.blocks);
    CHECK_EQ(w_long.blocks, 1 + 1999 * 2);
}

TEST(worst_case_login_count)
{
    uaf_work_t w = { 0, 0 };
    uint8_t salt[16] = { 0 }, out[32];

    uaf_hash(salt, "1234567", UAF_ITER_DEFAULT, out, &w);
    CHECK_EQ(w.blocks, 2000);                   /* 48 + 7 + 9 = 64: one block each */
    w.blocks = 0;
    uaf_hash(salt, "12345678", UAF_ITER_DEFAULT, out, &w);
    CHECK_EQ(w.blocks, 1 + 1999 * 2);
}

TEST(authorize_ops)
{
    uaf_db_t db;
    uint8_t s[16];
    size_t len;
    int idx;

    make_db(&db);
    salt_of(s, 9);
    CHECK_EQ(uaf_add(&db, "Oper", "X", 0, 0, s, "x"), UAF_E_EXISTS);
    CHECK_EQ(uaf_add(&db, "NEW-1", "X", 0, 0, s, "x"), UAF_E_NAME);
    CHECK_EQ(uaf_add(&db, "NEW", "TOOLONGHOME", 0, 0, s, "x"), UAF_E_HOME);
    CHECK_EQ(uaf_add(&db, "NEW", "X", 0, 0, s, ""), UAF_E_PASSWORD_LEN);
    CHECK_EQ(db.count, 3);
    CHECK_EQ(uaf_set_password(&db, "oper", s, "new pass"), UAF_OK);
    CHECK_MEM(db.rec[1].salt, s, 16);
    CHECK_EQ(uaf_verify(&db, "OPER", "new pass", &idx, NULL), UAF_OK);
    CHECK_EQ(uaf_verify(&db, "OPER", "op", &idx, NULL), UAF_E_BAD_LOGIN);
    CHECK_EQ(uaf_set_password(&db, "OPER", s, "\n"), UAF_E_PASSWORD_CHAR);
    CHECK_EQ(uaf_verify(&db, "OPER", "new pass", &idx, NULL), UAF_OK);
    CHECK_EQ(uaf_set_password(&db, "NOBODY", s, "x"), UAF_E_NO_USER);
    CHECK_EQ(uaf_set_privs(&db, "oper", UAF_PRIV_OPER | UAF_PRIV_READALL), UAF_OK);
    CHECK_EQ(db.rec[1].privs, 0xA);
    CHECK_EQ(uaf_set_privs(&db, "X Y", 0), UAF_E_NAME);
    CHECK_EQ(uaf_set_flags(&db, "NOBODY", 0), UAF_E_NO_USER);
    CHECK_EQ(uaf_set_home(&db, "OPER", "ops2"), UAF_OK);
    CHECK_STR(db.rec[1].home, "OPS2");
    CHECK_EQ(uaf_set_home(&db, "OPER", "a/b"), UAF_E_HOME);
    CHECK_EQ(uaf_remove(&db, "admin"), UAF_OK);
    CHECK_EQ(uaf_remove(&db, "admin"), UAF_E_NO_USER);
    CHECK_EQ(db.count, 2);
    CHECK_STR(db.rec[0].name, "OPER");
    CHECK_STR(db.rec[1].name, "GUEST");
    CHECK_EQ(db.rec[2].name[0], 0);
    CHECK_EQ(uaf_verify(&db, "GUEST", "guest", &idx, NULL), UAF_OK);
    CHECK_EQ(idx, 1);
    CHECK_EQ(uaf_write(&db, img, sizeof(img), &len), UAF_OK);
    CHECK_EQ(uaf_validate(img, len, NULL), UAF_OK);
    CHECK_EQ(uaf_db_init(&db, 0), UAF_E_ITERATIONS);
    CHECK_EQ(uaf_db_init(&db, 100001), UAF_E_ITERATIONS);
    CHECK_EQ(uaf_db_init(&db, 100000), UAF_OK);
}

TEST(privs_format)
{
    char buf[UAF_PRIVS_TEXT_MAX];
    char small[11];

    CHECK_EQ(uaf_privs_format(0, buf, sizeof(buf)), UAF_OK);
    CHECK_STR(buf, "");
    CHECK_EQ(uaf_privs_format(UAF_PRIV_OPER, buf, sizeof(buf)), UAF_OK);
    CHECK_STR(buf, "OPER");
    CHECK_EQ(uaf_privs_format(0xFFFFFFFFu, buf, sizeof(buf)), UAF_OK);
    CHECK_STR(buf, "SYSTEM,OPER,SECURITY,READALL");
    CHECK_EQ(strlen(buf) + 1, 29);
    CHECK_EQ(uaf_privs_format(UAF_PRIV_ALL, buf, 29), UAF_OK);
    CHECK_EQ(uaf_privs_format(UAF_PRIV_ALL, buf, 28), UAF_E_BUFFER);
    CHECK_STR(buf, "");
    CHECK_EQ(uaf_privs_format(UAF_PRIV_SYSTEM | UAF_PRIV_OPER, small, sizeof(small)), UAF_E_BUFFER);
    CHECK_EQ(uaf_privs_format(UAF_PRIV_SYSTEM | UAF_PRIV_OPER, buf, 12), UAF_OK);
    CHECK_EQ(uaf_privs_format(0, buf, 0), UAF_E_BUFFER);
    CHECK_STR(uaf_priv_name(2), "SECURITY");
    CHECK(uaf_priv_name(4) == NULL);
}

TEST(privs_parse)
{
    uint32_t p = 99;

    CHECK_EQ(uaf_privs_parse("", &p), UAF_OK);
    CHECK_EQ(p, 0);
    CHECK_EQ(uaf_privs_parse("()", &p), UAF_OK);
    CHECK_EQ(p, 0);
    CHECK_EQ(uaf_privs_parse("oper", &p), UAF_OK);
    CHECK_EQ(p, UAF_PRIV_OPER);
    CHECK_EQ(uaf_privs_parse(" ( OPER , security ) ", &p), UAF_OK);
    CHECK_EQ(p, UAF_PRIV_OPER | UAF_PRIV_SECURITY);
    CHECK_EQ(uaf_privs_parse("SYSTEM,OPER,SECURITY,READALL", &p), UAF_OK);
    CHECK_EQ(p, UAF_PRIV_ALL);
    CHECK_EQ(uaf_privs_parse("SY,R", &p), UAF_OK);
    CHECK_EQ(p, UAF_PRIV_SYSTEM | UAF_PRIV_READALL);
    p = 99;
    CHECK_EQ(uaf_privs_parse("S", &p), UAF_E_PRIV);     /* SYSTEM or SECURITY */
    CHECK_EQ(p, 99);
    CHECK_EQ(uaf_privs_parse("OPERX", &p), UAF_E_PRIV);
    CHECK_EQ(uaf_privs_parse("OPER,", &p), UAF_E_PRIV);
    CHECK_EQ(uaf_privs_parse(",OPER", &p), UAF_E_PRIV);
    CHECK_EQ(uaf_privs_parse("(OPER", &p), UAF_E_PRIV);
    CHECK_EQ(uaf_privs_parse("(", &p), UAF_E_PRIV);
    CHECK_EQ(uaf_privs_parse("ALL", &p), UAF_E_PRIV);
    CHECK_EQ(uaf_privs_parse(NULL, &p), UAF_E_PRIV);
}

TEST(strerror_text)
{
    CHECK_STR(uaf_strerror(UAF_OK), "ok");
    CHECK_STR(uaf_strerror(UAF_E_CRC), "user database CRC mismatch");
    CHECK_STR(uaf_strerror(UAF_E_BAD_LOGIN), "user authorization failure");
    CHECK_STR(uaf_strerror(UAF_E_NUM), "unknown user database error");
    for (int e = 0; e < UAF_E_NUM; e++) {
        CHECK(uaf_strerror((uaf_err_t)e) != NULL);
        CHECK(strcmp(uaf_strerror((uaf_err_t)e), "unknown user database error") != 0);
    }
}

TEST(default_card)
{
    static uint8_t buf[UAF_MAX_IMAGE + 1];
    FILE *f = fopen(UAF_CARD_PATH, "rb");
    uaf_db_t db;
    size_t len;
    int idx;

    CHECK(f != NULL);
    if (f == NULL) {
        return;
    }
    len = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    CHECK_EQ(uaf_load(&db, buf, len, NULL), UAF_OK);
    CHECK_EQ(db.count, 3);
    CHECK_EQ(db.iterations, UAF_ITER_DEFAULT);
    CHECK_EQ(uaf_verify(&db, "ADMIN", "ADMIN-CHANGE-ME", &idx, NULL), UAF_OK);
    CHECK_EQ(db.rec[idx].privs, UAF_PRIV_SYSTEM);
    CHECK_EQ(db.rec[idx].flags, UAF_FLAG_NOHACK);
    CHECK_EQ(uaf_verify(&db, "oper", "OPER-CHANGE-ME", &idx, NULL), UAF_OK);
    CHECK_EQ(db.rec[idx].privs, UAF_PRIV_OPER);
    CHECK_EQ(uaf_verify(&db, "GUEST", "GUEST", &idx, NULL), UAF_OK);
    CHECK_EQ(db.rec[idx].privs, 0);
    CHECK_EQ(db.rec[idx].flags & UAF_FLAG_NOHACK, 0);
}
