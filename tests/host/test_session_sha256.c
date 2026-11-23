#include "sha256.h"

#include "rc_test.h"

static void hex(const uint8_t *d, char *out)
{
    int i;

    for (i = 0; i < 32; i++) {
        snprintf(out + 2 * i, 3, "%02x", d[i]);
    }
}

static void check_digest(const void *msg, size_t len, const char *want)
{
    uint8_t d[32];
    char s[65];

    sha_digest(msg, len, d);
    hex(d, s);
    CHECK_STR(s, want);
}

TEST(empty)
{
    check_digest("", 0, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST(abc)
{
    check_digest("abc", 3, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST(msg_448_bits)
{
    const char *m = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";

    CHECK_EQ(strlen(m), 56);
    check_digest(m, 56, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST(msg_896_bits)
{
    const char *m = "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmno"
                    "ijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu";

    CHECK_EQ(strlen(m), 112);
    check_digest(m, 112, "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1");
}

TEST(million_a_streamed)
{
    uint8_t chunk[1000];
    uint8_t d[32];
    char s[65];
    sha_ctx_t c;
    int i;

    memset(chunk, 'a', sizeof(chunk));
    sha_init(&c);
    for (i = 0; i < 1000; i++) {
        sha_update(&c, chunk, i % 2 ? 1000 : 999);
        if (i % 2 == 0) {
            sha_update(&c, chunk, 1);
        }
    }
    sha_final(&c, d);
    hex(d, s);
    CHECK_STR(s, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    CHECK_EQ(c.blocks, 15626);    /* 1000000 bytes + 9 padding round up to 15626 blocks */
}

/* Expected digests of n bytes of 'a', from Python's hashlib. */
TEST(block_boundaries)
{
    static const struct { size_t n; const char *hex; unsigned blocks; } v[] = {
        { 55, "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318", 1 },
        { 56, "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a", 2 },
        { 63, "7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34", 2 },
        { 64, "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb", 2 },
        { 65, "635361c48bb9eab14198e76ea8ab7f1a41685d6ad62aa9146d301d4f17eb0ae0", 2 },
    };
    uint8_t buf[65];
    size_t i, split;

    memset(buf, 'a', sizeof(buf));
    for (i = 0; i < sizeof(v) / sizeof(v[0]); i++) {
        check_digest(buf, v[i].n, v[i].hex);
        for (split = 0; split <= v[i].n; split++) {
            uint8_t d[32];
            char s[65];
            sha_ctx_t c;

            sha_init(&c);
            sha_update(&c, buf, split);
            sha_update(&c, buf + split, v[i].n - split);
            sha_final(&c, d);
            hex(d, s);
            CHECK_STR(s, v[i].hex);
            CHECK_EQ(c.blocks, v[i].blocks);
        }
    }
}

TEST(zero_length_update)
{
    uint8_t d[32];
    char s[65];
    sha_ctx_t c;

    sha_init(&c);
    sha_update(&c, NULL, 0);
    sha_update(&c, "ab", 2);
    sha_update(&c, "", 0);
    sha_update(&c, "c", 1);
    sha_final(&c, d);
    hex(d, s);
    CHECK_STR(s, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST(length_carry)
{
    sha_ctx_t c;
    uint8_t d[32];

    sha_init(&c);
    c.len_lo = 0xFFFFFFFFu;
    sha_update(&c, "a", 1);
    CHECK_EQ(c.len_lo, 0);
    CHECK_EQ(c.len_hi, 1);
    sha_final(&c, d);
}
