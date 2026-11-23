#include "sha256.h"

#include <robco/klibc.h>

static const uint32_t sha_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

static uint32_t ror(uint32_t x, unsigned n)
{
    return (x >> n) | (x << (32 - n));
}

/*
 * One compression. The message schedule lives in a 16-word ring, so the
 * stack frame stays small: 64 bytes of schedule plus the working words.
 */
static void sha_compress(sha_ctx_t *ctx, const uint8_t *p)
{
    uint32_t w[16];
    uint32_t a = ctx->state[0], b = ctx->state[1], c = ctx->state[2], d = ctx->state[3];
    uint32_t e = ctx->state[4], f = ctx->state[5], g = ctx->state[6], h = ctx->state[7];
    unsigned i;

    for (i = 0; i < 16; i++) {
        w[i] = ((uint32_t)p[4 * i] << 24) | ((uint32_t)p[4 * i + 1] << 16) |
               ((uint32_t)p[4 * i + 2] << 8) | (uint32_t)p[4 * i + 3];
    }
    for (i = 0; i < 64; i++) {
        uint32_t t1, t2;

        if (i >= 16) {
            uint32_t w15 = w[(i - 15) & 15];
            uint32_t w2 = w[(i - 2) & 15];
            uint32_t s0 = ror(w15, 7) ^ ror(w15, 18) ^ (w15 >> 3);
            uint32_t s1 = ror(w2, 17) ^ ror(w2, 19) ^ (w2 >> 10);

            w[i & 15] += s0 + w[(i - 7) & 15] + s1;
        }
        t1 = h + (ror(e, 6) ^ ror(e, 11) ^ ror(e, 25)) + ((e & f) ^ (~e & g)) + sha_k[i] + w[i & 15];
        t2 = (ror(a, 2) ^ ror(a, 13) ^ ror(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
    ctx->blocks++;
}

void sha_init(sha_ctx_t *ctx)
{
    static const uint32_t iv[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
    };

    k_memcpy(ctx->state, iv, sizeof(iv));
    ctx->len_lo = 0;
    ctx->len_hi = 0;
    ctx->blocks = 0;
    ctx->buf_len = 0;
}

void sha_update(sha_ctx_t *ctx, const void *data, size_t len)
{
    const uint8_t *p = data;

    while (len > 0) {
        size_t n = SHA_BLOCK_SIZE - ctx->buf_len;
        uint32_t old = ctx->len_lo;

        if (n > len) {
            n = len;
        }
        k_memcpy(ctx->buf + ctx->buf_len, p, n);
        ctx->buf_len += (uint32_t)n;
        ctx->len_lo += (uint32_t)n;
        if (ctx->len_lo < old) {
            ctx->len_hi++;
        }
        p += n;
        len -= n;
        if (ctx->buf_len == SHA_BLOCK_SIZE) {
            sha_compress(ctx, ctx->buf);
            ctx->buf_len = 0;
        }
    }
}

void sha_final(sha_ctx_t *ctx, uint8_t out[SHA_DIGEST_SIZE])
{
    uint32_t bits_hi = (ctx->len_hi << 3) | (ctx->len_lo >> 29);
    uint32_t bits_lo = ctx->len_lo << 3;
    unsigned i;

    ctx->buf[ctx->buf_len++] = 0x80;
    if (ctx->buf_len > SHA_BLOCK_SIZE - 8) {
        k_memset(ctx->buf + ctx->buf_len, 0, SHA_BLOCK_SIZE - ctx->buf_len);
        sha_compress(ctx, ctx->buf);
        ctx->buf_len = 0;
    }
    k_memset(ctx->buf + ctx->buf_len, 0, SHA_BLOCK_SIZE - 8 - ctx->buf_len);
    for (i = 0; i < 4; i++) {
        ctx->buf[56 + i] = (uint8_t)(bits_hi >> (24 - 8 * i));
        ctx->buf[60 + i] = (uint8_t)(bits_lo >> (24 - 8 * i));
    }
    sha_compress(ctx, ctx->buf);
    ctx->buf_len = 0;
    for (i = 0; i < 8; i++) {
        out[4 * i] = (uint8_t)(ctx->state[i] >> 24);
        out[4 * i + 1] = (uint8_t)(ctx->state[i] >> 16);
        out[4 * i + 2] = (uint8_t)(ctx->state[i] >> 8);
        out[4 * i + 3] = (uint8_t)ctx->state[i];
    }
}

void sha_digest(const void *data, size_t len, uint8_t out[SHA_DIGEST_SIZE])
{
    sha_ctx_t ctx;

    sha_init(&ctx);
    sha_update(&ctx, data, len);
    sha_final(&ctx, out);
}
